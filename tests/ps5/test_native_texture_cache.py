#!/usr/bin/env python3
"""Exercise the actual native texture-cache header, including failed allocations."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <list>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

// Fail an actual list/string/hash-table allocation; the cache must roll back
// registration before reporting failure and leave the input handle untouched.
static int allocation_failure_after = -1;
void* operator new(std::size_t size) {
    if (allocation_failure_after == 0) throw std::bad_alloc();
    if (allocation_failure_after > 0) --allocation_failure_after;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
#if defined(__cpp_sized_deallocation)
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#endif

struct NVGcontext {};
static NVGcontext context;
static std::vector<int> deleted;
static std::function<void(int)> deletion_callback;
void nvgDeleteImage(NVGcontext* vg, int texture) {
    assert(vg == &context && texture > 0);
    assert(std::count(deleted.begin(), deleted.end(), texture) == 0);
    deleted.push_back(texture);
    if (deletion_callback) deletion_callback(texture);
}
namespace brls {
struct Event {
    std::vector<std::function<void()>> callbacks;
    template<class F> void subscribe(F fn) { callbacks.emplace_back(fn); }
    void fire() { for (auto& fn : callbacks) fn(); }
};
struct Application {
    static NVGcontext* getNVGContext() { return &context; }
    static Event* getWindowSizeChangedEvent() { static Event e; return &e; }
    static Event* getExitEvent() { static Event e; return &e; }
};
}
#include <borealis/core/cache_helper.hpp>
using Cache = brls::LRUCache<std::string, size_t>;
static void reset() {
    allocation_failure_after = -1;
    deleted.clear(); deletion_callback = {};
    brls::Application::getWindowSizeChangedEvent()->callbacks.clear();
    brls::Application::getExitEvent()->callbacks.clear();
}
static size_t refs(const Cache& cache, size_t value) {
    for (const auto& item : const_cast<Cache&>(cache).getCacheList())
        if (item.value == value) return item.count;
    std::abort();
}

#ifdef PS5_NATIVE_GPU
static void capacity_and_insert() {
    reset(); brls::TextureCache textures;
    auto& cache = textures.cache;
    assert(Cache::DEFAULT_CAPACITY == 32 && cache.getCapacity() == 32);
    for (int invalid : {-1, 0, 33, 400}) {
        bool threw = false;
        try { cache.setCapacity(invalid); } catch (const std::logic_error&) { threw = true; }
        assert(threw && cache.getCapacity() == 32);
    }
    for (size_t invalid : {size_t(0), size_t(33)}) {
        bool threw = false;
        try { Cache bad(invalid, 0); } catch (const std::logic_error&) { threw = true; }
        assert(threw);
    }
    for (size_t i = 1; i <= 1000; ++i) {
        assert(textures.tryAddCache(std::to_string(i), i));
        textures.removeCache(i);
        assert(cache.getCacheList().size() == std::min(i, size_t(32)));
    }
    assert(deleted.size() == 968 && deleted.front() == 1 && deleted.back() == 968);
    textures.clean(); assert(deleted.size() == 1000);
}
static void live_overflow_and_release() {
    for (size_t total : {size_t(1000), size_t(1001)}) {
        reset(); Cache cache(32, 0);
        for (size_t i = 1; i <= total; ++i) assert(cache.trySet(std::to_string(i), i));
        assert(cache.getCacheList().size() == total && deleted.empty());
        cache.setCapacity(1);
        assert(cache.getCacheList().size() == total && deleted.empty());
        for (size_t i = 1; i <= total; ++i) {
            cache.remove(i);
            assert(cache.getCacheList().size() == std::max(total - i, size_t(1)));
        }
        assert(deleted.size() == total - 1);
        for (size_t i = 0; i < deleted.size(); ++i) assert(deleted[i] == int(i + 1));
        cache.close(); assert(deleted.size() == total);
    }
}
static void stable_lru_and_bulk_shrink() {
    for (size_t total : {size_t(31), size_t(32)}) {
        reset(); Cache cache(32, 0);
        for (size_t i = 1; i <= total; ++i) { assert(cache.trySet(std::to_string(i), i)); cache.remove(i); }
        assert(cache.get("1") == 1); cache.remove(1);
        cache.setCapacity(1);
        assert(cache.getCacheList().size() == 1 && cache.getCacheList().front().value == 1);
        assert(deleted.size() == total - 1);
        for (size_t i = 0; i < deleted.size(); ++i) assert(deleted[i] == int(i + 2));
        cache.close();
    }
    reset(); Cache cache(4, 0);
    for (size_t i = 1; i <= 6; ++i) assert(cache.trySet(std::to_string(i), i));
    cache.remove(4); cache.remove(2);
    assert(deleted == std::vector<int>({4, 2}));
    // A live least-recent entry cannot prevent eviction of a newer released one.
    assert(cache.getCacheList().size() == 4 && refs(cache, 1) == 1);
    cache.close();
}
static void references_and_rejections() {
    reset(); brls::TextureCache textures;
    assert(!textures.tryAddCache("", 1));
    assert(!textures.tryAddCache("zero", 0));
    assert(!textures.tryAddCache("negative", size_t(-1)));
    assert(!textures.tryAddCache("too-large", size_t(std::numeric_limits<int>::max()) + 1));
    assert(textures.tryAddCache("one", 1));
    assert(!textures.tryAddCache("one", 2));
    assert(!textures.tryAddCache("alias", 1));
    assert(textures.cache.getCacheList().size() == 1 && refs(textures.cache, 1) == 1 && deleted.empty());
    assert(textures.getCache("missing") == 0 && textures.getCache("alias") == 0);
    assert(textures.getCache("one") == 1 && refs(textures.cache, 1) == 2);
    textures.removeCache(1); textures.removeCache(1); textures.removeCache(1);
    textures.removeCache(0); textures.removeCache(999);
    assert(refs(textures.cache, 1) == 0 && deleted.empty());
    assert(textures.getCache("one") == 1 && refs(textures.cache, 1) == 1);
    // Fault-inject a saturated counter; acquiring it must fail without wrapping.
    auto& node = const_cast<brls::Node<std::string, size_t>&>(textures.cache.getCacheList().front());
    node.count = std::numeric_limits<size_t>::max();
    assert(textures.getCache("one") == 0 && node.count == std::numeric_limits<size_t>::max());
    node.count = 1;
    textures.clean(); assert(deleted == std::vector<int>({1}));
}
static void dirty_replacement() {
    reset(); Cache cache(1, 0);
    assert(cache.trySet("same", 1));
    cache.markDirty(1); cache.markDirty(1); cache.markDirty(999);
    assert(cache.get("same") == 0 && refs(cache, 1) == 1);
    assert(cache.trySet("same", 2));
    assert(!cache.trySet("another", 1)); // Dirty handles remain owned.
    cache.remove(1);
    assert(deleted == std::vector<int>({1}) && cache.get("same") == 2);
    cache.remove(2); cache.remove(2);
    cache.markAllDirty(); cache.markAllDirty();
    assert(cache.get("same") == 0 && refs(cache, 2) == 0);
    assert(cache.trySet("same", 3));
    assert(deleted == std::vector<int>({1, 2}) && cache.get("same") == 3);
    cache.remove(2); // Retiring the old dirty generation cannot erase the new key.
    assert(cache.get("same") == 3 && refs(cache, 3) == 3);
    cache.close();
}
static void allocation_rollback() {
    for (size_t baseline : {size_t(0), size_t(1), size_t(13)}) {
        bool reached_success = false;
        size_t failure_count = 0;
        for (int fail_at = 0; fail_at < 32 && !reached_success; ++fail_at) {
            reset(); Cache cache(32, 0);
            for (size_t i = 1; i <= baseline; ++i) assert(cache.trySet(std::to_string(i), i));
            const std::string key(256, 'x');
            allocation_failure_after = fail_at;
            const bool admitted = cache.trySet(key, 99000);
            allocation_failure_after = -1;
            assert(deleted.empty());
            if (admitted) {
                reached_success = true;
                assert(cache.getCacheList().size() == baseline + 1 && refs(cache, 99000) == 1);
            } else {
                ++failure_count;
                assert(cache.getCacheList().size() == baseline && cache.get(key) == 0);
                for (size_t i = 1; i <= baseline; ++i) assert(refs(cache, i) == 1);
                // Both indexes must permit retry after a failed partial insertion.
                assert(cache.trySet(key, 99000) && refs(cache, 99000) == 1);
            }
            cache.close(); assert(deleted.size() == baseline + 1);
        }
        assert(reached_success && failure_count >= 4);
    }
}
static void close_and_events() {
    reset(); brls::TextureCache textures;
    assert(textures.tryAddCache("live", 1));
    assert(textures.tryAddCache("idle", 2)); textures.removeCache(2);
    brls::Application::getWindowSizeChangedEvent()->fire();
    assert(textures.getCache("live") == 0);
    assert(textures.tryAddCache("live", 3));
    deletion_callback = [&](int) {
        assert(textures.isClosing() && textures.cache.getCacheList().empty());
        assert(textures.getCache("live") == 0 && !textures.tryAddCache("reentrant", 4));
        textures.removeCache(1); textures.markDirty(1); textures.clean();
    };
    brls::Application::getExitEvent()->fire();
    assert(textures.isClosing() && textures.cache.getCacheList().empty() && deleted.size() == 3);
    const auto saved = deleted;
    deletion_callback = {};
    textures.removeCache(1); textures.removeCache(2); textures.removeCache(3);
    textures.cache.setCapacity(1); textures.markDirty(3);
    brls::Application::getWindowSizeChangedEvent()->fire();
    brls::Application::getExitEvent()->fire(); textures.clean();
    assert(!textures.tryAddCache("late", 4) && textures.getCache("live") == 0 && deleted == saved);
}
struct Expected { std::string key; size_t value, refs; bool dirty; };
static void model_workload() {
    reset(); Cache cache(32, 0);
    std::vector<Expected> model;
    std::vector<int> expected_deleted;
    size_t target = 32, next_handle = 1;
    uint32_t sequence = 0x714321u;
    const auto random = [&]() { sequence = sequence * 1664525u + 1013904223u; return sequence; };
    const auto trim_model = [&]() {
        while (model.size() > target) {
            auto item = std::find_if(model.rbegin(), model.rend(), [](const Expected& e) { return e.refs == 0; });
            if (item == model.rend()) break;
            expected_deleted.push_back(int(item->value));
            model.erase(std::next(item).base());
        }
    };
    for (size_t step = 0; step < 12000; ++step) {
        const auto op = random() % 9;
        const auto key = std::to_string(random() % 23);
        if (op <= 2) {
            const bool present = std::any_of(model.begin(), model.end(), [&](const Expected& e) { return e.key == key && !e.dirty; });
            assert(cache.trySet(key, next_handle) == !present);
            if (!present) { model.insert(model.begin(), {key, next_handle, 1, false}); trim_model(); }
            ++next_handle;
        } else if (op == 3) {
            auto item = std::find_if(model.begin(), model.end(), [&](const Expected& e) { return e.key == key && !e.dirty; });
            if (item == model.end()) assert(cache.get(key) == 0);
            else {
                Expected hit = *item; ++hit.refs;
                assert(cache.get(key) == hit.value);
                model.erase(item); model.insert(model.begin(), hit);
            }
        } else if (op <= 5 && !model.empty()) {
            auto& item = model[random() % model.size()];
            cache.remove(item.value);
            if (item.refs) { --item.refs; trim_model(); }
        } else if (op == 6 && !model.empty()) {
            auto& item = model[random() % model.size()];
            cache.markDirty(item.value); item.dirty = true;
        } else if (op == 7) {
            target = 1 + random() % 32; cache.setCapacity(int(target)); trim_model();
        } else if (op == 8) {
            cache.markAllDirty(); for (auto& item : model) item.dirty = true;
        }
        assert(cache.getCacheList().size() == model.size() && deleted == expected_deleted);
        auto actual = cache.getCacheList().begin();
        for (const auto& expected : model) {
            assert(actual->key == expected.key && actual->value == expected.value);
            assert(actual->count == expected.refs && actual->dirty == expected.dirty); ++actual;
        }
    }
    for (const auto& item : model) expected_deleted.push_back(int(item.value));
    cache.close(); assert(deleted == expected_deleted);
}
int main() {
    capacity_and_insert(); live_overflow_and_release(); stable_lru_and_bulk_shrink();
    references_and_rejections(); dirty_replacement(); allocation_rollback(); close_and_events(); model_workload();
    std::puts("native texture cache PASS: exact-target/live-overflow/LRU/leases/dirty/allocations/closing/model");
}
#else
int main() {
    reset(); Cache cache(200, 0);
    assert(Cache::DEFAULT_CAPACITY == 400);
    cache.setCapacity(1);
    for (size_t i = 1; i <= 100; ++i) { cache.set(std::to_string(i), i); cache.remove(i); }
    assert(cache.getCacheList().size() == 100 && deleted.empty());
    assert(cache.get("1") == 1 && refs(cache, 1) == 1);
    cache.update(1, 101); assert(cache.get("1") == 101);
    cache.markDirty(101); assert(cache.get("1") == 0);
    std::puts("legacy texture cache PASS: reserve/add/remap/dirty API unchanged");
}
#endif
'''


class NativeTextureCacheTests(unittest.TestCase):
    def test_actual_header_with_sanitizers_and_checked_iterators(self):
        compiler = shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(compiler, "C++ host compiler required")
        with tempfile.TemporaryDirectory(prefix="switchfin-texture-cache-") as work:
            source = Path(work) / "cache.cpp"
            source.write_text(HARNESS)
            common = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-g", "-O1",
                      "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                      "-I", str(ROOT / "library/include")]
            if sys.platform.startswith("linux"):
                common += ["-D_GLIBCXX_DEBUG", "-D_GLIBCXX_ASSERTIONS"]
            for branch in ("native", "legacy"):
                with self.subTest(branch=branch):
                    binary = Path(work) / branch
                    flags = ["-DPS5_NATIVE_GPU=1"] if branch == "native" else []
                    subprocess.run([*common, *flags, str(source), "-o", str(binary)], check=True)
                    subprocess.run([str(binary)], check=True, timeout=90,
                                   env={**os.environ, "ASAN_OPTIONS": "detect_leaks=" +
                                        ("0" if sys.platform == "darwin" else "1")})


if __name__ == "__main__":
    unittest.main()

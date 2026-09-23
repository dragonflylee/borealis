#!/usr/bin/env python3
"""Run actual native Image methods with strict cache/NanoVG ownership doubles."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
INCLUDE = ROOT / 'library/include'
SOURCE = ROOT / 'library/lib/views/image.cpp'

VIEW_STUB = r'''
#pragma once
#include <string>
#include <functional>
#include <cstddef>
struct NVGcontext {};
struct NVGpaint {};
namespace brls {
struct Style {};
struct FrameContext {};
class View {
public:
    virtual ~View() = default;
    virtual void draw(NVGcontext*, float, float, float, float, Style, FrameContext*) {}
    virtual void onLayout() {}
    void invalidate() { ++invalidations; }
    int invalidations = 0;
};
}
'''

HARNESS = r'''
#include <borealis/core/assets.hpp>
#include <borealis/views/image.hpp>
#include <cassert>
#include <climits>
#include <cstdio>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>
static NVGcontext context;
struct Allocation { int width = 32, height = 24, deletes = 0; };
static std::map<int, Allocation> allocations;
static int nextTexture = 1, createResult = INT_MAX, creates = 0, sizeQueries = 0;
static unsigned sizeWriteMask = 3;
static int allocate(int width = 32, int height = 24) {
    const int result = nextTexture++;
    allocations[result] = {width, height, 0};
    return result;
}
static bool alive(int texture) {
    return texture > 0 && allocations.count(texture) && allocations.at(texture).deletes == 0;
}
static void nvgDeleteImage(NVGcontext* vg, int texture) {
    assert(vg == &context && alive(texture));
    ++allocations.at(texture).deletes;
}
static void nvgImageSize(NVGcontext* vg, int texture, int* width, int* height) {
    assert(vg == &context && width && height && *width == 0 && *height == 0);
    assert(alive(texture)); ++sizeQueries;
    if (sizeWriteMask & 1) *width = allocations.at(texture).width;
    if (sizeWriteMask & 2) *height = allocations.at(texture).height;
}
static int create() {
    ++creates;
    if (createResult != INT_MAX) return createResult;
    return allocate();
}
static int nvgCreateImage(NVGcontext* vg, const char*, int) {
    assert(vg == &context); return create();
}
static int nvgCreateImageMem(NVGcontext* vg, int, unsigned char* data, int size) {
    assert(vg == &context && data && size > 0); return create();
}
#ifdef USE_LIBROMFS
namespace romfs {
static size_t resourceSize = 4;
static bool missing = false, nullData = false;
struct Resource {
    const unsigned char* data() const { static const unsigned char bytes[4] = {}; return nullData ? nullptr : bytes; }
    size_t size() const { return resourceSize; }
};
static const Resource& get(const std::string&) {
    if (missing) throw std::invalid_argument("missing resource");
    static Resource resource; return resource;
}
}
#endif
namespace brls {
struct Event { template<class F> void subscribe(F) {} };
struct Application {
    static NVGcontext* getNVGContext() { return &context; }
    static Event* getWindowSizeChangedEvent() { static Event event; return &event; }
    static Event* getExitEvent() { static Event event; return &event; }
};
struct Logger {
    template<class... Args> static void verbose(const char*, Args...) {
        throw std::logic_error("native ownership path must not log an acquired cache key");
    }
    template<class... Args> static void error(const char*, Args...) {}
};
class TextureCache {
public:
    std::map<std::string, int> keys;
    std::map<int, size_t> refs;
    bool closing = false, rejectInsert = false, pressure = false;
    int releases = 0;
    static TextureCache& instance() { static TextureCache cache; return cache; }
    bool isClosing() const { return closing; }
    bool tryAddCache(const std::string& key, size_t texture) {
        if (closing || rejectInsert || keys.count(key) || refs.count(texture)) return false;
        assert(!key.empty() && alive(texture));
        keys[key] = texture; refs[texture] = 1; return true;
    }
    int getCache(const std::string& key) {
        if (closing || !keys.count(key)) return 0;
        int texture = keys.at(key); assert(alive(texture)); ++refs.at(texture); return texture;
    }
    void markDirty(size_t texture) {
        if (closing) return;
        for (auto it = keys.begin(); it != keys.end();) {
            if (it->second == static_cast<int>(texture)) it = keys.erase(it); else ++it;
        }
    }
    void removeCache(size_t texture) {
        if (closing) return;
        assert(refs.count(texture) && refs.at(texture) > 0);
        ++releases; --refs.at(texture);
        if (pressure && refs.at(texture) == 0) {
            nvgDeleteImage(&context, texture);
            for (auto it = keys.begin(); it != keys.end();) {
                if (it->second == static_cast<int>(texture)) it = keys.erase(it); else ++it;
            }
            refs.erase(texture);
        }
    }
    void clean() {
        if (closing) return;
        closing = true;
        auto owned = std::move(refs); refs.clear(); keys.clear();
        for (const auto& entry : owned) nvgDeleteImage(&context, entry.first);
    }
};
Image::Image() = default;
void Image::draw(NVGcontext*, float, float, float, float, Style, FrameContext*) {}
void Image::onLayout() {}
int Image::getImageFlags() { return 0; }
ACTUAL_METHODS
class Probe : public Image {
public:
    size_t lookup(const std::string& key) { return checkCache(key); }
};
}
static int cached(const char* key) {
    const int texture = allocate();
    assert(brls::TextureCache::instance().tryAddCache(key, texture)); return texture;
}
int main(int argc, char** argv) {
    assert(argc == 2);
    std::string mode = argv[1]; auto& cache = brls::TextureCache::instance();
    if (mode == "shared") {
        int a = cached("a");
        { brls::Image first, second;
          first.setImageFromCache(a); second.setImageFromCache(cache.getCache("a"));
          assert(cache.refs.at(a) == 2 && alive(a));
          first.clear(); first.clear(); assert(cache.refs.at(a) == 1 && alive(a));
          second.setFreeTexture(true); }
        assert(cache.refs.at(a) == 0 && alive(a));
    } else if (mode == "same-cached") {
        int a = cached("a"); cache.pressure = true;
        { brls::Image view; view.setImageFromCache(a);
          for (int i = 0; i < 100; ++i) {
              view.setImageFromCache(cache.getCache("a"));
              assert(cache.refs.at(a) == 1 && alive(a));
          }
          view.setFreeTexture(false); view.innerSetImage(a);
          view.setFreeTexture(true); view.innerSetImage(a);
          assert(cache.refs.at(a) == 1 && alive(a)); }
        assert(!alive(a) && allocations.at(a).deletes == 1);
    } else if (mode == "replacement") {
        int a = cached("a"), b = cached("b"); cache.pressure = true;
        { brls::Image view; view.setImageFromCache(a); view.setImageFromCache(b);
          assert(!alive(a) && alive(b) && cache.refs.at(b) == 1); }
        assert(!alive(b));
    } else if (mode == "transitions") {
        int owned = allocate(), borrowed = allocate(), c = cached("c"), direct = allocate();
        { brls::Image view; view.innerSetImage(owned);
          view.setFreeTexture(false); view.innerSetImage(borrowed);
          assert(!alive(owned) && alive(borrowed));
          view.setImageFromCache(c); assert(alive(borrowed));
          view.setFreeTexture(true); view.innerSetImage(direct);
          assert(cache.refs.at(c) == 0 && alive(c));
          view.setFreeTexture(false); }
        assert(!alive(direct) && alive(borrowed));
        nvgDeleteImage(&context, borrowed);
    } else if (mode == "same-transfer") {
        int a = allocate(), b = allocate();
        { brls::Image view; view.innerSetImage(a);
          view.setFreeTexture(false); view.innerSetImage(a);
          assert(alive(a));
          assert(cache.tryAddCache("a", a)); view.setImageFromCache(a);
          assert(alive(a) && cache.refs.at(a) == 1);
          view.innerSetImage(b); // desired false, so b is borrowed
          assert(cache.refs.at(a) == 0 && alive(a));
          assert(cache.tryAddCache("b", b)); view.setImageFromCache(b); }
        assert(cache.refs.at(b) == 0 && alive(b));
    } else if (mode == "failed-file") {
        int a = cached("a"); cache.pressure = true;
        { brls::Probe view; view.setImageFromCache(a);
          assert(view.lookup("missing-svg") == 0);
          for (int failure : {0, -1}) {
              createResult = failure; view.setImageFromFile("missing");
              assert(view.getTexture() == a && cache.refs.at(a) == 1 && alive(a));
          }
          view.setImageFromCache(0); view.setImageFromCache(-1);
          assert(view.getTexture() == a && cache.refs.at(a) == 1); }
        assert(!alive(a));
    } else if (mode == "file-success") {
        int a = cached("a"); cache.pressure = true;
        { brls::Image view; view.setImageFromCache(a); view.setImageFromFile("file");
          int b = view.getTexture(); assert(b != a && !alive(a) && cache.refs.at(b) == 1);
          view.setImageFromFile("file");
          assert(view.getTexture() == b && creates == 1 && cache.refs.at(b) == 1); }
    } else if (mode == "rejected-insert") {
        int a = cached("a"); cache.rejectInsert = true;
        { brls::Image view; view.setImageFromCache(a); int incoming = nextTexture;
          view.setImageFromFile("new");
          assert(allocations.at(incoming).deletes == 1);
          assert(view.getTexture() == a && alive(a) && cache.refs.at(a) == 1);
          incoming = nextTexture; view.setImageFromRes("new-resource");
          assert(allocations.at(incoming).deletes == 1 && view.getTexture() == a); }
    } else if (mode == "memory") {
        static const unsigned char bytes[1] = {};
        int a = cached("a");
        { brls::Image view; view.setImageFromCache(a); view.setFreeTexture(false);
          view.setImageFromMem(nullptr, 1); view.setImageFromMem(bytes, 0);
          view.setImageFromMem(bytes, -1); assert(creates == 0);
          createResult = 0; view.setImageFromMem(bytes, 1);
          assert(view.getTexture() == a && cache.refs.at(a) == 1);
          createResult = INT_MAX; view.setImageFromMem(bytes, 1);
          int b = view.getTexture(); assert(b != a && cache.refs.at(a) == 0);
          view.clear(); assert(!alive(b)); }
    } else if (mode == "resource") {
        int a = cached("a");
        { brls::Image view; view.setImageFromCache(a);
#ifdef USE_LIBROMFS
          romfs::resourceSize = 0; view.setImageFromRes("empty");
          romfs::resourceSize = static_cast<size_t>(INT_MAX) + 1; view.setImageFromRes("large");
          assert(creates == 0 && view.getTexture() == a && cache.refs.at(a) == 1);
          romfs::resourceSize = 4; romfs::nullData = true; view.setImageFromRes("null");
          assert(creates == 0 && view.getTexture() == a && cache.refs.at(a) == 1);
          romfs::nullData = false; romfs::missing = true; view.setImageFromRes("missing");
          assert(view.getTexture() == a && cache.refs.at(a) == 1);
          romfs::missing = false; createResult = 0; view.setImageFromRes("invalid");
          assert(view.getTexture() == a && cache.refs.at(a) == 1);
          createResult = INT_MAX; view.setImageFromRes("ok"); int b = view.getTexture();
          assert(b != a && cache.refs.at(a) == 0 && cache.refs.at(b) == 1);
          view.setImageFromFile("@res/ok"); assert(view.getTexture() == b && cache.refs.at(b) == 1);
#else
          createResult = 0; view.setImageFromRes("invalid");
          assert(view.getTexture() == a && cache.refs.at(a) == 1);
          createResult = INT_MAX; view.setImageFromRes("ok"); int b = view.getTexture();
          assert(b != a && cache.refs.at(a) == 0 && cache.refs.at(b) == 1);
          view.setImageFromFile(std::string(BRLS_RESOURCES) + "ok");
          assert(view.getTexture() == b && cache.refs.at(b) == 1 && creates == 2);
#endif
        }
    } else if (mode == "dimensions") {
        int a = cached("a");
        { brls::Image view; view.setImageFromCache(a);
          for (auto shape : {std::pair<int,int>{0,10}, {-1,10}, {10,0}, {10,-1}}) {
              int b = allocate(shape.first, shape.second);
              view.setFreeTexture(true); view.innerSetImage(b);
              assert(!alive(b) && view.getTexture() == a && cache.refs.at(a) == 1);
          }
          for (unsigned mask : {0u, 1u, 2u}) {
              sizeWriteMask = mask; int b = allocate();
              view.innerSetImage(b); assert(!alive(b) && view.getTexture() == a);
          }
          sizeWriteMask = 3; int b = allocate(0, 1);
          assert(cache.tryAddCache("bad", b)); view.setImageFromCache(b);
          assert(cache.refs.at(b) == 0 && view.getTexture() == a && cache.getCache("bad") == 0);
          view.setFreeTexture(false); int borrowed = allocate(0, 1); view.innerSetImage(borrowed);
          assert(alive(borrowed) && view.getTexture() == a); nvgDeleteImage(&context, borrowed);
          sizeWriteMask = 0; view.setImageFromCache(cache.getCache("a"));
          assert(cache.refs.at(a) == 1 && view.getTexture() == a); sizeWriteMask = 3; }
    } else if (mode == "closing") {
        int a = cached("a"); static const unsigned char bytes[1] = {};
        { brls::Image view; view.setImageFromCache(a); cache.clean();
          assert(!alive(a)); int prior = sizeQueries;
          view.setImageFromFile("closed"); view.setImageFromRes("closed");
          view.setImageFromMem(bytes, 1); view.setImageFromCache(a);
          assert(creates == 0 && sizeQueries == prior);
          view.clear(); view.clear(); }
        assert(allocations.at(a).deletes == 1);
    } else return 2;
    cache.clean();
    for (const auto& item : allocations) assert(item.second.deletes == 1);
    std::printf("PASS actual native Image ownership: %s\n", mode.c_str());
}
'''

INTEGRATION_MAIN = r'''
static size_t count(int texture) {
    for (const auto& item : brls::TextureCache::instance().cache.getCacheList())
        if (item.value == static_cast<size_t>(texture)) return item.count;
    return 0;
}
int main() {
    auto& cache = brls::TextureCache::instance(); cache.cache.setCapacity(1);
    int a = cached("a"), b = 0, c = 0, direct = 0;
    { brls::Probe first, second;
      first.setImageFromCache(a); second.setImageFromCache(cache.getCache("a"));
      assert(count(a) == 2);
      first.setImageFromCache(cache.getCache("a")); assert(count(a) == 2);
      b = cached("b"); first.setImageFromCache(b);
      assert(alive(a) && count(a) == 1 && alive(b) && count(b) == 1);
      assert(cache.cache.getCacheList().size() == 2); // live overflow is retained
      // A rejected cached image must lose its lookup key even when the
      // retention target keeps its idle allocation resident. Retry can then
      // create and register a valid image under the same key.
      cache.cache.setCapacity(32);
      { brls::Image retry; retry.setImageFromCache(cache.getCache("b"));
        int invalid = allocate(0, 1); assert(cache.tryAddCache("retry", invalid));
        retry.setImageFromCache(invalid);
        assert(alive(invalid) && count(invalid) == 0 && retry.getTexture() == b);
        assert(cache.getCache("retry") == 0);
        retry.setImageFromFile("retry"); int repaired = retry.getTexture();
        assert(repaired != b && repaired != invalid && alive(repaired) && count(repaired) == 1);
        retry.clear(); }
      cache.cache.setCapacity(1);
      assert(cache.cache.getCacheList().size() == 2 && alive(a) && alive(b));
      createResult = 0; first.setImageFromFile("failed");
      assert(first.lookup("failed-svg") == 0 && first.getTexture() == b && count(b) == 1);
      createResult = INT_MAX; int rejected = nextTexture; first.setImageFromFile("");
      assert(!alive(rejected) && first.getTexture() == b && count(b) == 1);
      second.clear(); second.clear(); assert(!alive(a) && alive(b));
      cache.markDirty(b); c = cached("b"); first.setImageFromCache(c);
      assert(!alive(b) && alive(c) && count(c) == 1);
      first.setImageFromCache(cache.getCache("b")); assert(count(c) == 1);
      static const unsigned char bytes[1] = {}; first.setFreeTexture(false);
      first.setImageFromMem(bytes, 1); direct = first.getTexture();
      assert(direct != c && alive(direct) && count(c) == 0);
      cache.clean(); assert(!alive(c) && alive(direct));
      first.clear(); assert(!alive(direct));
    }
    cache.clean();
    for (const auto& item : allocations) assert(item.second.deletes == 1);
    assert(creates == 4 && sizeQueries == 8);
    std::puts("PASS actual Image plus actual native TextureCache: live overflow, failed replacement, dirty replacement, eviction and closing");
}
'''


def extract_function(source, signature):
    """Extract an existing definition, failing rather than substituting test logic."""
    if source.count(signature) != 1:
        raise AssertionError('Expected one production definition: ' + signature)
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
        end += 1
    return source[start:end]


class NativeImageOwnershipTests(unittest.TestCase):
    @staticmethod
    def actual_methods():
        source = SOURCE.read_text()
        signatures = ['size_t Image::checkCache(', 'void Image::setImageFromRes(',
                      'void Image::setImageFromFile(', 'void Image::setImageFromMem(',
                      'void Image::innerSetImage(', 'void Image::setImageFromCache(',
                      'void Image::replaceNativeTexture(', 'void Image::releaseNativeTexture(',
                      'void Image::clear()', 'void Image::setFreeTexture(',
                      'int Image::getTexture()', 'Image::~Image()']
        return '\n\n'.join(extract_function(source, item) for item in signatures)

    def test_actual_image_methods(self):
        compiler = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++')
        if not compiler:
            self.skipTest('host C++ compiler unavailable')
        methods = self.actual_methods()
        cases = ['shared', 'same-cached', 'replacement', 'transitions', 'same-transfer',
                 'failed-file', 'file-success', 'rejected-insert', 'memory', 'resource',
                 'dimensions', 'closing']
        with tempfile.TemporaryDirectory(prefix='switchfin-native-image-') as tmp:
            folder = Path(tmp)
            stub = folder / 'borealis/core/view.hpp'
            stub.parent.mkdir(parents=True)
            stub.write_text(VIEW_STUB)
            fixture = folder / 'fixture.cpp'
            fixture.write_text(HARNESS.replace('ACTUAL_METHODS', methods))
            binary = folder / 'fixture'
            command = [compiler, '-std=c++17', '-O1', '-g', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                       '-DPS5_NATIVE_GPU=1', '-DBRLS_RESOURCES="/app0/resources/"',
                       '-I' + str(folder), '-I' + str(INCLUDE),
                       '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(fixture), str(ROOT / 'library/lib/core/assets.cpp'), '-o', str(binary)]
            leaks = '0' if sys.platform == 'darwin' else '1'
            environment = dict(os.environ, ASAN_OPTIONS=f'detect_leaks={leaks}:halt_on_error=1',
                               UBSAN_OPTIONS='halt_on_error=1')
            for embedded in [False, True]:
                with self.subTest(embedded=embedded):
                    build = command + (['-DUSE_LIBROMFS=1'] if embedded else [])
                    result = subprocess.run(build, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                    self.assertEqual(result.returncode, 0, result.stdout)
                    for case in cases:
                        with self.subTest(case=case):
                            subprocess.run([str(binary), case], check=True, timeout=30, env=environment)

    def test_actual_image_and_cache_integration(self):
        compiler = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++')
        if not compiler:
            self.skipTest('host C++ compiler unavailable')
        prefix = HARNESS.split('class TextureCache {', 1)[0]
        suffix = 'Image::Image()' + HARNESS.split('Image::Image()', 1)[1].split('int main(', 1)[0]
        fixture_source = (prefix + '}\n#include <borealis/core/cache_helper.hpp>\nnamespace brls {\n' +
                          suffix + INTEGRATION_MAIN).replace('ACTUAL_METHODS', self.actual_methods())
        with tempfile.TemporaryDirectory(prefix='switchfin-native-image-cache-') as tmp:
            folder = Path(tmp)
            stub = folder / 'borealis/core/view.hpp'
            stub.parent.mkdir(parents=True)
            stub.write_text(VIEW_STUB)
            fixture = folder / 'fixture.cpp'
            fixture.write_text(fixture_source)
            binary = folder / 'fixture'
            command = [compiler, '-std=c++17', '-O1', '-g', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                       '-DPS5_NATIVE_GPU=1', '-DBRLS_RESOURCES="/app0/resources/"',
                       '-I' + str(folder), '-I' + str(INCLUDE),
                       '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(fixture), str(ROOT / 'library/lib/core/assets.cpp'), '-o', str(binary)]
            result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            self.assertEqual(result.returncode, 0, result.stdout)
            leaks = '0' if sys.platform == 'darwin' else '1'
            subprocess.run([str(binary)], check=True, timeout=30,
                           env=dict(os.environ, ASAN_OPTIONS=f'detect_leaks={leaks}:halt_on_error=1',
                                    UBSAN_OPTIONS='halt_on_error=1'))


if __name__ == '__main__':
    unittest.main()

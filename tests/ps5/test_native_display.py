#!/usr/bin/env python3
"""Exercise the real immutable display helper and Borealis constructor guard."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
INCLUDE = ROOT / 'library/include'
VIDEO = ROOT / 'library/lib/platforms/sdl/sdl_video.cpp'
HEADER = 'borealis/platforms/ps5/native_display.hpp'

HARNESS = r'''
#include <borealis/platforms/ps5/native_display.hpp>
#include <array>
#include <cassert>
#include <climits>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace brls {
namespace nd = ps5_native_display;
struct SDL_Window {};
static SDL_Window fixtureWindow;
static std::array<int, 4> response;
static unsigned writeMask;
static int queries, publications, viewports, errors, finishes;
static double scaleFactor;
static std::array<int, 2> published, viewport;
static void SDL_GetWindowSize(SDL_Window* window, int* width, int* height) {
    assert(window == &fixtureWindow && width && height && *width == 0 && *height == 0);
    assert(queries++ == 0);
    if (writeMask & 1) *width = response[0];
    if (writeMask & 2) *height = response[1];
}
static void SDL_GL_GetDrawableSize(SDL_Window* window, int* width, int* height) {
    assert(window == &fixtureWindow && width && height && *width == 0 && *height == 0);
    assert(queries++ == 1);
    if (writeMask & 4) *width = response[2];
    if (writeMask & 8) *height = response[3];
}
struct Application {
    static void setWindowSize(int width, int height) {
        assert(queries == 2 && width == nd::width && height == nd::height);
        ++publications; published = {width, height};
    }
};
static void glViewport(int x, int y, int width, int height) {
    assert(queries == 2 && publications == 1 && x == 0 && y == 0);
    assert(width == nd::width && height == nd::height);
    ++viewports; viewport = {width, height};
}
struct Logger {
    template<class... Args> static void error(const char*, Args...) { ++errors; }
};
[[noreturn]] static void fatal(const char* message) { throw std::runtime_error(message); }
struct ConstructorFixture {
    SDL_Window* window = &fixtureWindow;
    void setup() {
ACTUAL_WINDOW_STATE_BLOCK
        // This continuation is reachable only after the production guard.
        assert(width == nd::width && height == nd::height);
        assert(fWidth == nd::width && fHeight == nd::height);
        ++finishes;
    }
};

static void run(const std::array<int, 4>& values, unsigned writes, bool success) {
    response = values; writeMask = writes;
    queries = publications = viewports = errors = finishes = 0;
    scaleFactor = 7.0;
    published = {-11, -12}; viewport = {-21, -22};
    bool threw = false;
    try { ConstructorFixture{}.setup(); }
    catch (const std::runtime_error& error) {
        threw = true;
        assert(std::string(error.what()) == "sdl: native display dimensions do not match configured profile");
    }
    assert(queries == 2 && threw != success);
    if (success) {
        assert(publications == 1 && viewports == 1 && errors == 0 && finishes == 1);
        assert(scaleFactor == 1.0);
        assert((published == std::array<int, 2>{nd::width, nd::height}));
        assert((viewport == std::array<int, 2>{nd::width, nd::height}));
    } else {
        assert(publications == 0 && viewports == 0 && errors == 1 && finishes == 0);
        assert(scaleFactor == 7.0);
        assert((published == std::array<int, 2>{-11, -12}));
        assert((viewport == std::array<int, 2>{-21, -22}));
    }
}
} // namespace brls

int main() {
    using namespace brls;
    const std::array<int, 4> correct{nd::width, nd::height, nd::width, nd::height};
    int cases = 0;
    run(correct, 15, true); ++cases;
    for (unsigned mask = 0; mask < 15; ++mask) { run(correct, mask, false); ++cases; }
    for (unsigned field = 0; field < 4; ++field) {
        const int expected = correct[field];
        for (int bad : {0, -1, INT_MIN, INT_MAX, 1, expected - 1, expected + 1, expected / 2}) {
            auto values = correct; values[field] = bad;
            run(values, 15, false); ++cases;
        }
    }
    // A coherent but different display profile must not be silently accepted.
    const int otherWidth = nd::width == 1920 ? 3840 : 1920;
    const int otherHeight = nd::height == 1080 ? 2160 : 1080;
    run({otherWidth, otherHeight, otherWidth, otherHeight}, 15, false); ++cases;
    run(correct, 15, true); ++cases;

    // Reusing previously valid evidence must still zero every query output.
    nd::Dimensions stale{nd::width, nd::height, nd::width, nd::height};
    int applied = 0, queried = 0;
    auto unwritten = [&](int* w, int* h) { assert(*w == 0 && *h == 0); ++queried; };
    assert(!nd::queryAndApply(stale, unwritten, unwritten,
        [&](const nd::Dimensions&) { ++applied; }));
    assert(queried == 2 && applied == 0);
    assert(stale.windowWidth == 0 && stale.windowHeight == 0 &&
        stale.drawableWidth == 0 && stale.drawableHeight == 0);
    std::printf("PASS actual native display guard: %dx%d nominal %dHz, %d constructor cases, stale evidence reset\n",
        nd::width, nd::height, nd::refreshHz, cases);
}
'''


class NativeDisplayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++')
        if not cls.compiler:
            raise unittest.SkipTest('host C++ compiler unavailable')
        text = VIDEO.read_text()
        start = '    // Setup window state\n'
        end = '\n    int xPos, yPos;'
        if text.count(start) != 1 or text.count(end) != 1:
            raise AssertionError('Expected one actual constructor window-state block')
        cls.block = text.split(start, 1)[1].split(end, 1)[0]

    def compile(self, folder, source, config=None, native=True, syntax=False):
        folder.mkdir(parents=True, exist_ok=True)
        src = folder / 'fixture.cpp'
        src.write_text(source)
        if config is not None:
            (folder / 'ps5_native_display_config.hpp').write_text(config)
        command = [self.compiler, '-std=c++17', '-O1', '-g', '-UNDEBUG',
                   '-Wall', '-Wextra', '-Werror', '-I' + str(INCLUDE), '-I' + str(folder),
                   '-DPS5=1', '-DBOREALIS_USE_OPENGL=1']
        if native:
            command.append('-DPS5_NATIVE_GPU=1')
        if syntax:
            command.append('-fsyntax-only')
        else:
            command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-o', str(folder / 'fixture')]
        command.append(str(src))
        return subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    @staticmethod
    def config(width, height, refresh):
        return (f'#define PS5_NATIVE_DISPLAY_WIDTH {width}\n'
                f'#define PS5_NATIVE_DISPLAY_HEIGHT {height}\n'
                f'#define PS5_NATIVE_DISPLAY_REFRESH_HZ {refresh}\n')

    def test_actual_constructor_guard_for_both_profiles(self):
        with tempfile.TemporaryDirectory(prefix='switchfin-display-') as tmp:
            for width, height in [(1920, 1080), (3840, 2160)]:
                with self.subTest(width=width, height=height):
                    folder = Path(tmp) / str(height)
                    result = self.compile(folder, HARNESS.replace('ACTUAL_WINDOW_STATE_BLOCK', self.block),
                                          self.config(width, height, 60))
                    self.assertEqual(result.returncode, 0, result.stdout)
                    leaks = '0' if sys.platform == 'darwin' else '1'
                    subprocess.run([str(folder / 'fixture')], check=True, timeout=30,
                                   env=dict(os.environ, ASAN_OPTIONS=f'detect_leaks={leaks}:halt_on_error=1',
                                            UBSAN_OPTIONS='halt_on_error=1'))

    def test_unsupported_missing_and_malformed_configuration_is_rejected(self):
        invalid = [None, self.config(1920, 2160, 60), self.config(3840, 1080, 60),
                   self.config(1280, 720, 60), self.config(1920, 1080, 120),
                   self.config(0, 1080, 60), self.config(-1920, 1080, 60),
                   self.config('1920.5', 1080, 60), self.config(1920, 1080, '60.5'),
                   self.config('"1920"', 1080, 60), self.config('2147483648', 1080, 60),
                   '#define PS5_NATIVE_DISPLAY_WIDTH 1920\n#define PS5_NATIVE_DISPLAY_HEIGHT 1080\n']
        source = f'#include <{HEADER}>\nint main() {{ return 0; }}\n'
        with tempfile.TemporaryDirectory(prefix='switchfin-display-invalid-') as tmp:
            for index, config in enumerate(invalid):
                with self.subTest(index=index):
                    result = self.compile(Path(tmp) / str(index), source, config, syntax=True)
                    self.assertNotEqual(result.returncode, 0, 'Malformed native configuration accepted')

    def test_non_native_header_has_no_generated_configuration_dependency(self):
        source = f'''#include <{HEADER}>
#ifdef PS5_NATIVE_DISPLAY_WIDTH
#error Non-native build consumed native display configuration
#endif
int main() {{ return 0; }}
'''
        with tempfile.TemporaryDirectory(prefix='switchfin-display-nonnative-') as tmp:
            result = self.compile(Path(tmp), source, native=False)
            self.assertEqual(result.returncode, 0, result.stdout)
            subprocess.run([str(Path(tmp) / 'fixture')], check=True, timeout=30)


if __name__ == '__main__':
    unittest.main()

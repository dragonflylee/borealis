#!/usr/bin/env python3
"""Actual native NanoVG texture callbacks with GL faults and Image/cache integration.

This verifies application ownership/error handling, not real GPU storage or retirement.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from test_native_image_ownership import extract_function, VIEW_STUB

ROOT = Path(__file__).resolve().parents[2]
INCLUDE = ROOT / 'library/include'
NANOVG = INCLUDE / 'borealis/extern/nanovg/nanovg_gl.h'
IMAGE = ROOT / 'library/lib/views/image.cpp'

HARNESS = r'''
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <nanovg.h>
using GLuint = unsigned int;
using GLenum = unsigned int;
using GLint = int;
using GLsizei = int;
enum : GLenum {
    GL_NO_ERROR = 0, GL_OUT_OF_MEMORY = 0x505, GL_INVALID_OPERATION = 0x502,
    GL_TEXTURE_2D = 0xde1, GL_UNPACK_ALIGNMENT = 0xcf5, GL_UNPACK_ROW_LENGTH = 0xcf2,
    GL_UNPACK_SKIP_PIXELS = 0xcf4, GL_UNPACK_SKIP_ROWS = 0xcf3,
    GL_RGBA8 = 0x8058, GL_RGBA = 0x1908, GL_RED = 0x1903, GL_UNSIGNED_BYTE = 0x1401,
    GL_TEXTURE_MIN_FILTER = 0x2801, GL_TEXTURE_MAG_FILTER = 0x2800,
    GL_NEAREST_MIPMAP_NEAREST = 0x2700, GL_LINEAR_MIPMAP_LINEAR = 0x2703,
    GL_NEAREST = 0x2600, GL_LINEAR = 0x2601, GL_TEXTURE_WRAP_S = 0x2802,
    GL_TEXTURE_WRAP_T = 0x2803, GL_REPEAT = 0x2901, GL_CLAMP_TO_EDGE = 0x812f,
    GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT = 0x8a34
};
ACTUAL_ENUMS
ACTUAL_STRUCTS
static GLNVGcontext glContext{};
struct NVGcontext { NVGparams params{}; };
static NVGcontext context;
struct Name { int deletes = 0, uploads = 0, width = 0, height = 0; bool imported = false; };
static std::map<GLuint, Name> names;
static GLuint nextName = 10, boundName = 0, lastGenerated = 0;
static GLenum pendingError = GL_NO_ERROR;
static int generations = 0, uploads = 0, parameters = 0, mipmaps = 0, finishes = 0;
static int shaderCreates = 0, bufferCreates = 0;
static int alignment = 4, rowLength = 0, skipPixels = 0, skipRows = 0;
static bool failAlloc = false;
static std::string fault;
static bool live(GLuint name) { return name && names.count(name) && !names.at(name).deletes; }
static void error(GLenum code = GL_OUT_OF_MEMORY) { if (!pendingError) pendingError = code; }
static GLenum glGetError() { GLenum result = pendingError; pendingError = GL_NO_ERROR; return result; }
static void glGenTextures(GLsizei count, GLuint* output) {
    assert(count == 1); ++generations;
    if (fault == "gen-zero" || fault == "gen-zero-error") {
        *output = 0; if (fault == "gen-zero-error") error(); return;
    }
    *output = nextName++; lastGenerated = *output; names.emplace(*output, Name{});
    if (fault == "gen-error") error();
}
static void glDeleteTextures(GLsizei count, const GLuint* input) {
    assert(count == 1 && live(*input)); ++names.at(*input).deletes;
    if (boundName == *input) boundName = 0;
}
static void glBindTexture(GLenum target, GLuint name) {
    assert(target == GL_TEXTURE_2D && (!name || live(name)));
    if ((fault == "bind-error" && name) || (fault == "unbind-error" && !name)) { error(); return; }
    boundName = name;
}
static void glPixelStorei(GLenum property, GLint value) {
    if (fault == "unpack-error" && property == GL_UNPACK_ALIGNMENT && value == 1) { error(); return; }
    if (property == GL_UNPACK_ALIGNMENT) alignment = value;
    else if (property == GL_UNPACK_ROW_LENGTH) rowLength = value;
    else if (property == GL_UNPACK_SKIP_PIXELS) skipPixels = value;
    else if (property == GL_UNPACK_SKIP_ROWS) skipRows = value;
    else assert(false);
    if (fault == "restore-error" && property == GL_UNPACK_ALIGNMENT && value == 4) error();
}
static void glTexImage2D(GLenum target, GLint level, GLint internal, GLsizei width, GLsizei height,
                          GLint border, GLenum format, GLenum type, const void*) {
    assert(target == GL_TEXTURE_2D && level == 0 && border == 0 && type == GL_UNSIGNED_BYTE);
    assert(format == GL_RGBA || format == GL_RED);
    assert(internal == static_cast<GLint>(format == GL_RED && width <= 1024 && height <= 1024 ? GL_RGBA8 : format));
    ++uploads;
    assert(live(boundName));
    assert(alignment == 1 && rowLength == width && skipPixels == 0 && skipRows == 0);
    if (fault == "upload-error") { error(); return; }
    auto& name = names.at(boundName); ++name.uploads; name.width = width; name.height = height;
}
static void glTexParameteri(GLenum target, GLenum, GLint) {
    assert(target == GL_TEXTURE_2D); ++parameters;
    if (fault == "parameter-error") error(GL_INVALID_OPERATION);
}
static void glGenerateMipmap(GLenum target) {
    assert(target == GL_TEXTURE_2D); ++mipmaps;
    if (fault == "mipmap-error") error();
}
static void glGenVertexArrays(GLsizei n, GLuint* output) { assert(n == 1); *output = 200; }
static void glGenBuffers(GLsizei n, GLuint* output) { assert(n == 1); ++bufferCreates; *output = 300; }
static void glUniformBlockBinding(GLuint, GLuint, GLuint binding) { assert(binding == 0); }
static void glGetIntegerv(GLenum property, GLint* output) {
    assert(property == GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT); *output = 16;
}
static void glFinish() { ++finishes; }
static void* allocation(void* old, std::size_t size) { return failAlloc ? nullptr : std::realloc(old, size); }
#define realloc allocation
ACTUAL_ALLOC_FIND_DELETE_BIND
#undef realloc
ACTUAL_CHECK_ERROR
static int glnvg__createShader(GLNVGshader*, const char*, const char*, const char*, const char*, const char*) {
    ++shaderCreates; return 1;
}
static void glnvg__getUniforms(GLNVGshader*) {}
ACTUAL_CREATE_TEXTURE
ACTUAL_CREATE
ACTUAL_DELETE_CALLBACK
ACTUAL_SIZE_CALLBACK
extern "C" NVGparams* nvgInternalParams(NVGcontext* ctx) { return &ctx->params; }
ACTUAL_IMPORT
extern "C" void nvgDeleteImage(NVGcontext* ctx, int image) {
    assert(ctx == &context); assert(glnvg__renderDeleteTexture(&glContext, image));
}
extern "C" void nvgImageSize(NVGcontext* ctx, int image, int* width, int* height) {
    assert(ctx == &context && *width == 0 && *height == 0);
    (void)glnvg__renderGetTextureSize(&glContext, image, width, height);
}
extern "C" int nvgCreateImage(NVGcontext* ctx, const char*, int flags) {
    assert(ctx == &context); return glnvg__renderCreateTexture(&glContext, NVG_TEXTURE_RGBA, 32, 24, flags, nullptr);
}
extern "C" int nvgCreateImageMem(NVGcontext* ctx, int flags, unsigned char* data, int size) {
    assert(ctx == &context && data && size > 0);
    return glnvg__renderCreateTexture(&glContext, NVG_TEXTURE_RGBA, 32, 24, flags, data);
}
#include <borealis/views/image.hpp>
namespace brls {
struct Event { template<class F> void subscribe(F) {} };
struct Application {
    static NVGcontext* getNVGContext() { return &context; }
    static Event* getWindowSizeChangedEvent() { static Event event; return &event; }
    static Event* getExitEvent() { static Event event; return &event; }
};
struct Logger {
    template<class... A> static void error(const char*, A...) { static_assert(sizeof...(A) == 0); }
    template<class... A> static void verbose(const char*, A...) { static_assert(sizeof...(A) == 0); }
};
}
#include <borealis/core/cache_helper.hpp>
namespace brls {
Image::Image() = default;
void Image::draw(NVGcontext*, float, float, float, float, Style, FrameContext*) {}
void Image::onLayout() {}
int Image::getImageFlags() { return 0; }
ACTUAL_IMAGE_METHODS
}
static int create(int flags = 0, int width = 32, int height = 24, int type = NVG_TEXTURE_RGBA) {
    static const unsigned char bytes[4] = {};
    return glnvg__renderCreateTexture(&glContext, type, width, height, flags, bytes);
}
static void setup(int debug) {
    context.params.userPtr = &glContext; glContext.flags = debug;
}
static void clearError() { fault.clear(); while (glGetError() != GL_NO_ERROR) {} }
static void emptySlots() {
    for (int i = 0; i < glContext.ntextures; ++i) assert(glContext.textures[i].id == 0 && glContext.textures[i].tex == 0);
}
static void restored() {
    assert(alignment == 4 && rowLength == 0 && skipPixels == 0 && skipRows == 0);
    assert(boundName == 0);
#if NANOVG_GL_USE_STATE_FILTER
    assert(glContext.boundTexture == 0);
#endif
}
static void clean() {
    clearError();
    for (int i = 0; i < glContext.ntextures; ++i) {
        if (glContext.textures[i].id) glnvg__deleteTexture(&glContext, glContext.textures[i].id);
    }
    std::free(glContext.textures); glContext.textures = nullptr;
    for (const auto& item : names) {
        if (item.second.imported) assert(item.second.deletes == 0);
        else assert(item.second.deletes == 1);
    }
}
static void failureCase(const std::string& mode) {
    fault = mode;
    const int before = glContext.textureId;
    assert(create(NVG_IMAGE_GENERATE_MIPMAPS | NVG_IMAGE_NODELETE) == 0);
    if (mode == "gen-zero-error") assert(pendingError == GL_NO_ERROR);
    if (mode == "unpack-error") assert(uploads == 0);
    if (mode != "gen-zero" && mode != "gen-zero-error") assert(!live(lastGenerated));
    if (mode.find("gen-") != 0) restored();
    emptySlots(); assert(!glnvg__findTexture(&glContext, before + 1));
    const int slotCount = glContext.ntextures;
    fault.clear();
    // If rollback itself raised a GL error, it is not claimed as repaired:
    // the next attempt must reject that pending error without allocating.
    if (pendingError != GL_NO_ERROR) {
        const int beforeGenerations = generations;
        assert(create() == 0 && generations == beforeGenerations);
    }
    int next = create();
    assert(next > before + 1 && glContext.ntextures == slotCount && live(lastGenerated));
    assert(glnvg__renderDeleteTexture(&glContext, next));
    assert(!glnvg__renderDeleteTexture(&glContext, next)); emptySlots();
}
int main(int argc, char** argv) {
    assert(argc == 3); setup(std::atoi(argv[2]) ? NVG_DEBUG : 0);
    const std::string mode = argv[1];
    if (mode == "dimensions") {
        for (const auto& pair : std::vector<std::pair<int,int>>{{0, 1}, {1, 0}, {-1, 1}, {1, -1}, {INT_MIN, 4}})
            assert(create(0, pair.first, pair.second) == 0);
        assert(!glContext.textures && generations == 0 && uploads == 0 && glContext.textureId == 0);
    } else if (mode == "prior-error") {
        pendingError = GL_INVALID_OPERATION;
        assert(create() == 0 && generations == 0 && glContext.textureId == 0 && !glContext.textures);
        assert(create() > 0); restored();
    } else if (mode == "allocation") {
        failAlloc = true; assert(create() == 0 && generations == 0 && !glContext.textures);
        failAlloc = false; assert(create() > 0);
    } else if (mode == "success") {
        int id = create(NVG_IMAGE_GENERATE_MIPMAPS | NVG_IMAGE_NEAREST | NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY, 3840, 2160);
        assert(id > 0 && uploads == 1 && mipmaps == 1 && parameters == 4);
        auto* texture = glnvg__findTexture(&glContext, id);
        assert(texture && texture->width == 3840 && texture->height == 2160 && names.at(texture->tex).uploads == 1);
        int width = 0, height = 0; assert(glnvg__renderGetTextureSize(&glContext, id, &width, &height));
        assert(width == 3840 && height == 2160); restored();
        assert(create(0, 1, 1, NVG_TEXTURE_ALPHA) > id); restored();
    } else if (mode == "alpha-backing-bound") {
        // Exercise the real callback around both size boundaries; logical
        // alpha type and byte stride remain unchanged in either storage path.
        for (const auto& size : std::vector<std::pair<int,int>>{{1,1},{512,512},{1024,1024},{1025,512},{512,1025},{2048,2048}}) {
            const int id = create(0, size.first, size.second, NVG_TEXTURE_ALPHA);
            const auto* texture = glnvg__findTexture(&glContext, id);
            assert(texture && texture->type == NVG_TEXTURE_ALPHA);
            restored();
        }
    } else if (mode == "success-nodelete") {
        const int id = create(NVG_IMAGE_NODELETE); assert(id > 0);
        const GLuint name = glnvg__findTexture(&glContext, id)->tex;
        assert(glnvg__renderDeleteTexture(&glContext, id) && live(name));
        glDeleteTextures(1, &name); // Ownership transferred to the successful caller.
    } else if (mode == "borrowed") {
        const GLuint imported = nextName++; names[imported].imported = true;
        const int borrowed = nvglCreateImageFromHandleGL3(&context, imported, 80, 60, NVG_IMAGE_NODELETE);
        assert(borrowed > 0 && generations == 0);
        fault = "upload-error"; assert(create(NVG_IMAGE_NODELETE) == 0);
        assert(glnvg__findTexture(&glContext, borrowed)->tex == imported && live(imported));
        assert(names.at(imported).uploads == 0);
        clearError(); assert(glnvg__renderDeleteTexture(&glContext, borrowed) && live(imported));
        const int next = create(); assert(next > borrowed && !glnvg__findTexture(&glContext, borrowed));
    } else if (mode == "bind-error") {
        const GLuint imported = nextName++; names[imported].imported = true;
        const int borrowed = nvglCreateImageFromHandleGL3(&context, imported, 80, 60, NVG_IMAGE_NODELETE);
        glnvg__bindTexture(&glContext, imported);
        fault = mode; assert(create() == 0);
        assert(names.at(imported).uploads == 0 && names.at(imported).deletes == 0 && uploads == 0);
        assert(glnvg__findTexture(&glContext, borrowed)->tex == imported);
        assert(!live(lastGenerated));
    } else if (mode == "dummy-failure" || mode == "dummy-gen-failure") {
        fault = mode == "dummy-failure" ? "upload-error" : "gen-zero-error";
        assert(!glnvg__renderCreate(&glContext));
        assert(glContext.dummyTex == 0 && shaderCreates == 1 && bufferCreates == 2 && finishes == 0);
        emptySlots(); restored();
    } else if (mode == "dummy-success") {
        assert(glnvg__renderCreate(&glContext));
        assert(glContext.dummyTex > 0 && shaderCreates == 1 && finishes == 1);
        auto* texture = glnvg__findTexture(&glContext, glContext.dummyTex);
        assert(texture && texture->width == 1 && texture->height == 1 && texture->type == NVG_TEXTURE_ALPHA); restored();
    } else if (mode == "image-integration") {
        auto& cache = brls::TextureCache::instance();
        {
            brls::Image image; image.setImageFromFile("old-artwork");
            const int old = image.getTexture(); auto* oldSlot = glnvg__findTexture(&glContext, old);
            assert(oldSlot); const GLuint oldName = oldSlot->tex;
            fault = "upload-error"; image.setImageFromFile("replacement-artwork");
            assert(image.getTexture() == old && live(oldName) && !live(lastGenerated));
            assert(cache.getCache("replacement-artwork") == 0);
            static const unsigned char bytes[4] = {}; image.setImageFromMem(bytes, 4);
            assert(image.getTexture() == old && live(oldName) && !live(lastGenerated));
            clearError(); image.setImageFromFile("replacement-artwork");
            assert(image.getTexture() != old && live(oldName));
            const int replacement = image.getTexture(); const GLuint newName = glnvg__findTexture(&glContext, replacement)->tex;
            image.setImageFromFile("replacement-artwork"); assert(image.getTexture() == replacement);
            cache.clean(); assert(!live(oldName) && !live(newName));
        }
    } else {
        failureCase(mode);
    }
    clean(); std::printf("PASS actual NanoVG upload/Image integration: %s debug=%s state-filter=%d\n", argv[1], argv[2], NANOVG_GL_USE_STATE_FILTER);
}
'''


def definition(text, prefix):
    """Select a definition rather than the callback forward declaration."""
    start = text.index(prefix)
    while text[text.index('\n', start) - 1] == ';':
        start = text.index(prefix, start + len(prefix))
    opening = text.index('{', start)
    signature = text[start:opening + 1]
    return extract_function(text, signature)


class NativeTextureUploadTests(unittest.TestCase):
    def test_actual_callbacks_and_image_ownership(self):
        compiler = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler, 'host C++ compiler required')
        source = NANOVG.read_text()
        image_source = IMAGE.read_text()
        image_methods = '\n'.join(extract_function(image_source, sig) for sig in [
            'size_t Image::checkCache(', 'void Image::setImageFromFile(', 'void Image::setImageFromMem(',
            'void Image::innerSetImage(', 'void Image::setImageFromCache(', 'void Image::replaceNativeTexture(',
            'void Image::releaseNativeTexture(', 'void Image::clear()', 'void Image::setFreeTexture(',
            'int Image::getTexture()', 'Image::~Image()'])
        start = source.index('int nvglCreateImageFromHandleGL3(NVGcontext* ctx, GLuint textureId, int w, int h, int imageFlags)')
        opening = source.index('{', start)
        imported = extract_function(source, source[start:opening + 1])
        imported = source[start:source.index('\n', start)] + '\n' + imported[imported.index('{'):]
        values = {
            'ACTUAL_ENUMS': extract_function(source, 'enum NVGcreateFlags {') + ';\n' + extract_function(source, 'enum NVGimageFlagsGL {') + ';',
            'ACTUAL_STRUCTS': source[source.index('enum GLNVGuniformLoc {'):source.index('static int glnvg__maxi')],
            'ACTUAL_ALLOC_FIND_DELETE_BIND': '\n'.join(definition(source, sig) for sig in [
                'static int glnvg__maxi(', 'static GLNVGtexture* glnvg__allocTexture(',
                'static GLNVGtexture* glnvg__findTexture(', 'static int glnvg__deleteTexture(',
                'static void glnvg__bindTexture(']),
            'ACTUAL_CHECK_ERROR': definition(source, 'static void glnvg__checkError('),
            'ACTUAL_CREATE_TEXTURE': definition(source, 'static int glnvg__renderCreateTexture('),
            'ACTUAL_CREATE': definition(source, 'static int glnvg__renderCreate('),
            'ACTUAL_DELETE_CALLBACK': definition(source, 'static int glnvg__renderDeleteTexture('),
            'ACTUAL_SIZE_CALLBACK': definition(source, 'static int glnvg__renderGetTextureSize('),
            'ACTUAL_IMPORT': imported,
            'ACTUAL_IMAGE_METHODS': image_methods,
        }
        fixture = HARNESS
        # Longer markers first: ACTUAL_CREATE is a prefix of ACTUAL_CREATE_TEXTURE.
        for marker in sorted(values, key=len, reverse=True):
            fixture = fixture.replace(marker, values[marker])
        modes = ['dimensions', 'prior-error', 'allocation', 'success', 'success-nodelete', 'borrowed', 'bind-error',
                 'gen-zero', 'gen-zero-error', 'gen-error', 'upload-error', 'parameter-error',
                 'mipmap-error', 'unpack-error', 'restore-error', 'unbind-error',
                 'dummy-failure', 'dummy-gen-failure', 'dummy-success', 'image-integration', 'alpha-backing-bound']
        with tempfile.TemporaryDirectory(prefix='switchfin-texture-upload-') as work:
            folder = Path(work)
            view = folder / 'borealis/core/view.hpp'; view.parent.mkdir(parents=True)
            view.write_text(VIEW_STUB.replace('struct NVGcontext {};', '#include <nanovg.h>').replace('struct NVGpaint {};', ''))
            driver = folder / 'fixture.cpp'; driver.write_text(fixture)
            binary = folder / 'fixture'
            command = [compiler, '-std=c++17', '-O1', '-g', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                       '-Wno-unused-parameter',
                       '-DPS5_NATIVE_GPU=1', '-DNANOVG_GL3=1', '-DNANOVG_GL_USE_UNIFORMBUFFER=1',
                       '-I' + str(folder), '-I' + str(INCLUDE), '-I' + str(NANOVG.parent),
                       '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(driver), '-o', str(binary)]
            environment = dict(os.environ, ASAN_OPTIONS='detect_leaks=' + ('0' if sys.platform == 'darwin' else '1') + ':halt_on_error=1',
                               UBSAN_OPTIONS='halt_on_error=1')
            for state_filter in [0, 1]:
                with self.subTest(state_filter=state_filter):
                    subprocess.run(command + ['-DNANOVG_GL_USE_STATE_FILTER=' + str(state_filter)], check=True)
                    for debug in ['0', '1']:
                        for mode in modes:
                            with self.subTest(mode=mode, debug=debug):
                                subprocess.run([str(binary), mode, debug], check=True, timeout=30, env=environment)


if __name__ == '__main__':
    unittest.main()

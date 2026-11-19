/*
    Copyright 2021 natinusala

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <borealis/core/application.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/core/thread.hpp>
#include <borealis/platforms/sdl/sdl_video.hpp>
#ifdef PS5_NATIVE_GPU
#include <borealis/platforms/ps5/native_display.hpp>
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#ifdef BOREALIS_USE_OPENGL
#ifdef __PSV__
#include <GLES2/gl2.h>
extern "C"
{
#include <gpu_es4/psp2_pvr_hint.h>
#include <psp2/kernel/modulemgr.h>
}
#define NANOVG_GLES2_IMPLEMENTATION
#elif defined(PS4)
#include <orbis/Pigletv2VSH.h>
#define NANOVG_GLES2_IMPLEMENTATION
#else
#include <glad/glad.h>
#ifdef USE_GLES2
#define NANOVG_GLES2_IMPLEMENTATION
#elif USE_GLES3
#define NANOVG_GLES3_IMPLEMENTATION
#elif USE_GL2
#define NANOVG_GL2_IMPLEMENTATION
#else
#define NANOVG_GL3_IMPLEMENTATION
#endif
#endif
#include <nanovg_gl.h>
#ifdef PS5_NATIVE_HDR
#include <borealis/platforms/ps5/native_hdr.hpp>
#include <EGL/egl.h>
// Application-owned, source-built runtime selector; not a firmware ABI.
extern "C" int ps5ExperimentalSelectHdrScanout(int enabled);
#endif
#elif defined(BOREALIS_USE_D3D11)
#include <nanovg_d3d11.h>

#include <borealis/platforms/driver/d3d11.hpp>
std::unique_ptr<brls::D3D11Context> D3D11_CONTEXT;
#endif

namespace brls
{

static double scaleFactor = 1.0;

#ifndef PS5_NATIVE_GPU
static void sdlWindowFramebufferSizeCallback(SDL_Window* window, int width, int height)
{
    if (!width || !height)
        return;

    int fWidth, fHeight;
#ifdef BOREALIS_USE_OPENGL
    SDL_GL_GetDrawableSize(window, &fWidth, &fHeight);
    scaleFactor = fWidth * 1.0 / width;
#if defined(ANDROID)
    // On Android, doing this is to ensure that glViewport is called from the main thread
    brls::sync([fWidth, fHeight]()
        { glViewport(0, 0, fWidth, fHeight); });
#else
    glViewport(0, 0, fWidth, fHeight);
#endif
#elif defined(BOREALIS_USE_D3D11)
    fWidth      = width;
    fHeight     = height;
    scaleFactor = D3D11_CONTEXT->getScaleFactor();
    D3D11_CONTEXT->onFramebufferSize(fWidth, fHeight);
#endif

    Application::onWindowResized(fWidth, fHeight);

    if (!VideoContext::FULLSCREEN)
    {
        VideoContext::sizeW = width;
        VideoContext::sizeH = height;
    }
}

static void sdlWindowPositionCallback(SDL_Window* window, int windowXPos, int windowYPos)
{
    Application::onWindowReposition(windowXPos, windowYPos);

    if (!VideoContext::FULLSCREEN)
    {
        VideoContext::posX = (float)windowXPos;
        VideoContext::posY = (float)windowYPos;
    }
}

static int sdlWindowEventWatcher(void* data, SDL_Event* event)
{
    if (event->type == SDL_WINDOWEVENT)
    {
        SDL_Window* win = SDL_GetWindowFromID(event->window.windowID);
        switch (event->window.event)
        {
            case SDL_WINDOWEVENT_RESIZED:
                if (win == (SDL_Window*)data)
                {
                    sdlWindowFramebufferSizeCallback(win,
                        event->window.data1,
                        event->window.data2);
                }
                break;
            case SDL_WINDOWEVENT_MOVED:
                if (win == (SDL_Window*)data)
                {
                    sdlWindowPositionCallback(win,
                        event->window.data1,
                        event->window.data2);
                }
                break;
        }
    }
    return 0;
}
#endif

SDLVideoContext::SDLVideoContext(std::string windowTitle, uint32_t windowWidth, uint32_t windowHeight, float windowXPos, float windowYPos)
{
#ifdef PS5_NATIVE_GPU
    try
    {
#endif
#ifdef __PSV__
#define MAX_PATH 256
    /// Huge thanks to SonicMastr for his kindness help and contribution in psv homebrew.

    windowWidth  = 960;
    windowHeight = 544;

    /* Disable Back Touchpad to prevent "misclicks" */
    SDL_setenv("VITA_DISABLE_TOUCH_BACK", "1", 1);

    /* We need to use some custom hints */
    SDL_setenv("VITA_PVR_SKIP_INIT", "yeet", 1);

    PVRSRV_PSP2_APPHINT hint;
    char target_path[MAX_PATH];
    const char* default_path = "app0:module";

    /* Load Modules */
    sceKernelLoadStartModule("vs0:sys/external/libfios2.suprx", 0, NULL, 0, NULL, NULL);
    sceKernelLoadStartModule("vs0:sys/external/libc.suprx", 0, NULL, 0, NULL, NULL);
    snprintf(target_path, MAX_PATH, "%s/%s", default_path, "libgpu_es4_ext.suprx");
    sceKernelLoadStartModule(target_path, 0, NULL, 0, NULL, NULL);
    snprintf(target_path, MAX_PATH, "%s/%s", default_path, "libIMGEGL.suprx");
    sceKernelLoadStartModule(target_path, 0, NULL, 0, NULL, NULL);

    /* Set PVR Hints */
    PVRSRVInitializeAppHint(&hint);
    snprintf(hint.szGLES1, MAX_PATH, "%s/%s", default_path, "libGLESv1_CM.suprx");
    snprintf(hint.szGLES2, MAX_PATH, "%s/%s", default_path, "libGLESv2.suprx");
    snprintf(hint.szWindowSystem, MAX_PATH, "%s/%s", default_path, "libpvrPSP2_WSEGL.suprx");

    hint.ui32SwTexOpCleanupDelay = 32000; // Set to 32 milliseconds to prevent a pool of unfreed memory
    PVRSRVCreateVirtualAppHint(&hint);
#endif

#ifdef PS5_NATIVE_HDR
    if (ps5ExperimentalSelectHdrScanout(1) != 0)
        fatal("native HDR: could not select ten-bit presentation");
    this->hdrSelected = true;
#endif
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
#ifdef PS5_NATIVE_GPU
        fatal(std::string("sdl: failed to initialize video: ") + SDL_GetError());
#else
        Logger::error("sdl: failed to initialize");
        return;
#endif
    }

    // Create window
#ifdef PS5_NATIVE_GPU
    // One immutable native EGL Core 3.3 window, selected with its dependencies.
    windowWidth = ps5_native_display::width;
    windowHeight = ps5_native_display::height;
    Uint32 windowFlags = SDL_WINDOW_SHOWN;
#else
    Uint32 windowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI;
#endif
#ifdef BOREALIS_USE_OPENGL
#ifdef PS5_NATIVE_GPU
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
#ifdef PS5_NATIVE_HDR
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 10);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 10);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 10);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 2);
#else
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
#endif
#elif defined(__SWITCH__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_RETAINED_BACKING, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#elif defined(__PSV__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_RETAINED_BACKING, 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#elif defined(USE_GLES2)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_RETAINED_BACKING, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#elif defined(USE_GLES3)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_RETAINED_BACKING, 0);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#elif defined(USE_GL2)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_RETAINED_BACKING, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 5);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 6);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 5);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
#endif
#if defined(USE_EGL)
    SDL_SetHint(SDL_HINT_VIDEO_X11_FORCE_EGL, "1");
#endif
    windowFlags |= SDL_WINDOW_OPENGL;
#endif
#ifndef PS5_NATIVE_GPU
    if (VideoContext::FULLSCREEN)
    {
#ifdef __WINRT__
        windowFlags |= SDL_WINDOW_FULLSCREEN;
#else
        windowFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif
    }
#endif
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");

    if (std::isnan(windowXPos) || std::isnan(windowYPos))
    {
        this->window = SDL_CreateWindow(windowTitle.c_str(),
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            windowWidth,
            windowHeight,
            windowFlags);
    }
    else
    {
        this->window = SDL_CreateWindow(windowTitle.c_str(),
            windowXPos > 0 ? windowXPos : SDL_WINDOWPOS_UNDEFINED,
            windowYPos > 0 ? windowYPos : SDL_WINDOWPOS_UNDEFINED,
            windowWidth,
            windowHeight,
            windowFlags);
    }

    if (!this->window)
    {
        fatal("sdl: failed to create window");
    }
#ifdef BOREALIS_USE_OPENGL
    // Configure window
#ifdef PS5_NATIVE_GPU
    this->glContext = SDL_GL_CreateContext(window);
    if (!this->glContext)
        fatal(std::string("sdl: failed to create GL context: ") + SDL_GetError());
    if (SDL_GL_MakeCurrent(window, this->glContext) != 0)
        fatal(std::string("sdl: failed to bind GL context: ") + SDL_GetError());
#else
    SDL_GLContext context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, context);
#endif
#endif
#ifndef PS5_NATIVE_GPU
    SDL_AddEventWatch(sdlWindowEventWatcher, window);
#endif
#ifdef BOREALIS_USE_OPENGL
#if !defined(__PSV__) && !defined(PS4)
    // Load OpenGL routines using glad
#ifdef PS5_NATIVE_GPU
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
        fatal("sdl: failed to load OpenGL entry points");
#else
    gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress);
#endif
#endif

    Logger::info("sdl: GL Vendor: {}", (const char*)glGetString(GL_VENDOR));
    Logger::info("sdl: GL Renderer: {}", (const char*)glGetString(GL_RENDERER));
    Logger::info("sdl: GL Version: {}", (const char*)glGetString(GL_VERSION));

#ifdef PS5_NATIVE_HDR
    // Query the selected EGL configuration, not the requested SDL attributes.
    const EGLDisplay display = eglGetCurrentDisplay();
    const EGLContext context = eglGetCurrentContext();
    EGLint configId = 0, count = 0;
    if (!eglQueryContext(display, context, EGL_CONFIG_ID, &configId))
        fatal("native HDR: unavailable EGL configuration");
    const EGLint attributes[] = {EGL_CONFIG_ID, configId, EGL_NONE};
    EGLConfig config = nullptr;
    if (!eglChooseConfig(display, attributes, &config, 1, &count) || count != 1)
        fatal("native HDR: unavailable EGL configuration");
    for (EGLint attribute : {EGL_RED_SIZE, EGL_GREEN_SIZE, EGL_BLUE_SIZE, EGL_ALPHA_SIZE}) {
        EGLint bits = 0;
        if (!eglGetConfigAttrib(display, config, attribute, &bits) ||
            bits != (attribute == EGL_ALPHA_SIZE ? 2 : 10))
            fatal("native HDR: ten-bit EGL configuration required");
    }
    int drawableWidth = 0, drawableHeight = 0;
    SDL_GL_GetDrawableSize(this->window, &drawableWidth, &drawableHeight);
    if (drawableWidth != int(windowWidth) || drawableHeight != int(windowHeight))
        fatal("native HDR: drawable geometry differs from compiled display");
    this->hdrFrame = std::make_unique<ps5_native_hdr::Frame>();
    if (!this->hdrFrame->open(windowWidth, windowHeight))
        fatal("native HDR: could not allocate linear composition target");
    Logger::info("native HDR: linear composition {}x{}, image bytes {}",
        windowWidth, windowHeight, this->hdrFrame->imageBytes());
#endif

    // Initialize nanovg
#ifdef __PSV__
    this->nvgContext = nvgCreateGLES2(0);
#elif PS4
    // Same as GLES2, but with pre-compiled shaders, so the flags must be "NVG_STENCIL_STROKES | NVG_ANTIALIAS" now
    this->nvgContext = nvgCreateGLES2(NVG_STENCIL_STROKES | NVG_ANTIALIAS);
#elif USE_GLES2
    this->nvgContext = nvgCreateGLES2(NVG_STENCIL_STROKES | NVG_ANTIALIAS);
#elif USE_GLES3
    this->nvgContext = nvgCreateGLES3(NVG_STENCIL_STROKES | NVG_ANTIALIAS);
#elif USE_GL2
    this->nvgContext = nvgCreateGL2(NVG_STENCIL_STROKES | NVG_ANTIALIAS);
#else
    this->nvgContext = nvgCreateGL3(NVG_STENCIL_STROKES | NVG_ANTIALIAS);
#endif
#elif defined(BOREALIS_USE_D3D11)
    Logger::info("sdl: USE_D3D11");
    D3D11_CONTEXT    = std::make_unique<D3D11Context>(this->window, windowWidth, windowHeight);
    this->nvgContext = nvgCreateD3D11(D3D11_CONTEXT->getDevice(), NVG_ANTIALIAS | NVG_STENCIL_STROKES);
#endif
    if (!this->nvgContext)
    {
        brls::fatal("sdl: unable to init nanovg");
    }

    setSwapInterval(VideoContext::swapInterval);

    // Setup window state
#ifdef PS5_NATIVE_GPU
    int width = 0, height = 0;
    int fWidth = 0, fHeight = 0;
    ps5_native_display::Dimensions observed;
    if (!ps5_native_display::queryAndApply(observed,
        [this](int* w, int* h) { SDL_GetWindowSize(window, w, h); },
        [this](int* w, int* h) { SDL_GL_GetDrawableSize(window, w, h); },
        [&](const ps5_native_display::Dimensions& actual) {
            width = actual.windowWidth;
            height = actual.windowHeight;
            fWidth = actual.drawableWidth;
            fHeight = actual.drawableHeight;
            scaleFactor = fWidth * 1.0 / width;
            Application::setWindowSize(fWidth, fHeight);
            glViewport(0, 0, fWidth, fHeight);
        }))
    {
        Logger::error("ps5 native: configured {}x{}, queried window {}x{}, drawable {}x{}",
            ps5_native_display::width, ps5_native_display::height,
            observed.windowWidth, observed.windowHeight,
            observed.drawableWidth, observed.drawableHeight);
        fatal("sdl: native display dimensions do not match configured profile");
    }
#else
    int width, height;
    SDL_GetWindowSize(window, &width, &height);

    int fWidth, fHeight;
#ifdef BOREALIS_USE_OPENGL
    SDL_GL_GetDrawableSize(window, &fWidth, &fHeight);
    scaleFactor = fWidth * 1.0 / width;
    Application::setWindowSize(fWidth, fHeight);
    glViewport(0, 0, fWidth, fHeight);
#elif defined(BOREALIS_USE_D3D11)
    scaleFactor      = D3D11_CONTEXT->getScaleFactor();
    fWidth           = width;
    fHeight          = height;
    Application::setWindowSize(fWidth, fHeight);
    D3D11_CONTEXT->onFramebufferSize(fWidth, fHeight);
#endif
#endif

    int xPos, yPos;
    SDL_GetWindowPosition(window, &xPos, &yPos);
    Application::setWindowPosition(xPos, yPos);

    if (!VideoContext::FULLSCREEN)
    {
        VideoContext::sizeW = width;
        VideoContext::sizeH = height;
        VideoContext::posX  = (float)xPos;
        VideoContext::posY  = (float)yPos;
    }
#ifdef PS5_NATIVE_GPU
    }
    catch (...)
    {
        this->cleanup();
        throw;
    }
#endif
}

void SDLVideoContext::beginFrame()
{
#ifdef PS5_NATIVE_HDR
    if (!hdrFrame || !hdrFrame->begin()) {
        invalidateRender();
        fatal("native HDR: composition target unavailable");
    }
    hdrClearNow = hdrFrame->modeState().beginFrame();
#endif
#if defined(BOREALIS_USE_D3D11)
    D3D11_CONTEXT->beginFrame();
#endif
}

#ifdef PS5_NATIVE_HDR
uint32_t SDLVideoContext::getLinearHdrFramebuffer() const
{
    return hdrFrame && isRenderAvailable() ? hdrFrame->framebuffer() : 0;
}
bool SDLVideoContext::selectHdrVideoTarget(uint32_t& framebuffer, int& internalFormat)
{
    if (!hdrFrame || !isRenderAvailable() || !hdrFrame->framebuffer()) return false;
    if (hdrFrame->modeState().videoTarget()) {
        // Direct: mpv's PQ output is the whole window image this frame.
        framebuffer = 0;
        internalFormat = GL_RGB10_A2;
        return true;
    }
    // Composite: the UI draws over a transparent linear layer.
    glClearColor(0, 0, 0, 0);
    if (!hdrFrame->clearComposition() || !hdrFrame->videoFramebuffer()) return false;
    framebuffer = hdrFrame->videoFramebuffer();
    internalFormat = GL_RGBA16F;
    return true;
}

void SDLVideoContext::hdrBeforeUiFlush(NVGcontext* vg)
{
    if (!hdrFrame) return;
    // Vertex bounds are only needed on video frames; menus use full-frame composition.
    const bool queued = vg && nvglPendingCallCountGL3(vg) > 0;
    bool small = false;
    bool windowSafe = false;
    if (queued && hdrFrame->modeState().videoFrame()) {
        float bounds[4], view[2];
        small = nvglPendingBoundsGL3(vg, bounds, view) &&
                hdrFrame->selectUiRegion(bounds[0], bounds[1], bounds[2], bounds[3], view[0], view[1]);
        windowSafe = small && nvglPendingStencilFreeGL3(vg) != 0;
    }
    if (vg) nvglSetHdrPqOutputGL3(vg, 0);
    switch (hdrFrame->modeState().beforeUiFlush(queued, small, windowSafe)) {
    case ps5_native_hdr::UiPreparation::WindowDirect:
        // Draw the UI into the window itself, in PQ, over the video mpv just wrote.
        nvglSetHdrPqOutputGL3(vg, 1);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, Application::windowWidth, Application::windowHeight);
        return;
    case ps5_native_hdr::UiPreparation::BoundedClear: {
        if (!hdrFrame->clearCompositionRegion()) {
            invalidateRender();
            fatal("native HDR: composition target unavailable");
        }
        return;
    }
    case ps5_native_hdr::UiPreparation::None: return;
    case ps5_native_hdr::UiPreparation::OpaqueClear:
        ps5_native_hdr::clearColor(hdrClearColor.r, hdrClearColor.g, hdrClearColor.b, hdrClearColor.a);
        break;
    case ps5_native_hdr::UiPreparation::TransparentClear:
        glClearColor(0, 0, 0, 0);
        break;
    }
    {
        if (!hdrFrame->clearComposition()) {
            invalidateRender();
            fatal("native HDR: composition target unavailable");
        }
    }
}


#endif

void SDLVideoContext::endFrame()
{
#ifdef BOREALIS_USE_OPENGL
#ifdef PS5_NATIVE_HDR
    // Real mpv video/subtitles and deferred NanoVG UI have finished drawing to
    // the same linear target. Encode once, before the actual platform swap.
    {
    if (!hdrFrame || !hdrFrame->present()) {
        Logger::error("native HDR: PQ presentation failed");
        invalidateRender();
        Application::quit();
        return;
    }
    }
#endif
#ifdef PS5
    // SDL2's public swap function returns void; its video backend reports a
    // failed flip through SDL_SetError. Do not report that frame to the player.
    SDL_ClearError();
#endif
    {
    SDL_GL_SwapWindow(this->window);
    }
#ifdef PS5
    if (*SDL_GetError())
    {
        Logger::error("ps5: presentation failed: {}", SDL_GetError());
        invalidateRender();
        Application::quit();
        return;
    }
#endif
#ifdef PS5_NATIVE_GPU
    notifyPresented();
#endif
#elif defined(BOREALIS_USE_D3D11)
    D3D11_CONTEXT->endFrame();
#endif
}

void SDLVideoContext::setSwapInterval(int interval)
{
#ifdef PS5_NATIVE_GPU
    interval = 1;
#endif
    VideoContext::swapInterval = interval;
#ifdef BOREALIS_USE_OPENGL
#ifdef PS5_NATIVE_GPU
    if (SDL_GL_SetSwapInterval(interval) != 0)
        fatal(std::string("native fixed display swap interval unavailable: ") + SDL_GetError());
#else
    SDL_GL_SetSwapInterval(interval);
#endif
#elif defined(BOREALIS_USE_D3D11)
    D3D11_CONTEXT->setSwapInterval(interval);
#endif
}

void SDLVideoContext::clear(NVGcolor color)
{
#ifdef BOREALIS_USE_OPENGL
#ifdef PS5_NATIVE_HDR
    // A likely video frame defers the UI clear until the video target is known.
    hdrClearColor = color;
    if (!hdrClearNow) return;
    ps5_native_hdr::clearColor(color.r, color.g, color.b, color.a);
#else
    glClearColor(
        color.r,
        color.g,
        color.b,
        color.a);
#endif

#ifdef PS5_NATIVE_GPU
    // The native window requests no depth buffer. NanoVG and mpv's GL
    // renderer do not use depth testing, but the native renderer supplies a combined
    // attachment: clearing its unused depth plane writes/flushes it on CPU.
    // Establish the full color/stencil clear even after a renderer leaves
    // scissor or write masks behind. Stencil is required for NanoVG clipping.
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilMask(0xffffffff);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
#else
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
#endif
#elif defined(BOREALIS_USE_D3D11)
    D3D11_CONTEXT->clear(nvgRGBAf(
        color.r,
        color.g,
        color.b,
        color.a));
#endif
}

void SDLVideoContext::resetState()
{
#ifdef BOREALIS_USE_OPENGL
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
#endif
}

double SDLVideoContext::getScaleFactor()
{
    return scaleFactor;
}

SDLVideoContext::~SDLVideoContext()
{
#ifdef PS5_NATIVE_GPU
    this->cleanup();
}

void SDLVideoContext::cleanup()
{
#endif
#ifdef PS5_NATIVE_HDR
    if (hdrFrame) {
        if (!hdrFrame->close()) {
            Logger::error("native HDR: retaining graphics owners after completion failure");
            return;
        }
        hdrFrame.reset();
    }
#endif
    try
    {
        if (this->nvgContext)
        {
#ifdef BOREALIS_USE_OPENGL
#ifdef USE_GLES2
            nvgDeleteGLES2(this->nvgContext);
#elif USE_GLES3
            nvgDeleteGLES3(this->nvgContext);
#elif USE_GL2
            nvgDeleteGL2(this->nvgContext);
#else
            nvgDeleteGL3(this->nvgContext);
#endif
#elif defined(BOREALIS_USE_D3D11)
            nvgDeleteD3D11(this->nvgContext);
            D3D11_CONTEXT = nullptr;
#endif
        }
    }
    catch (...)
    {
        Logger::error("Cannot delete nvg Context");
    }
#if defined(PS5_NATIVE_GPU) && defined(BOREALIS_USE_OPENGL)
    if (this->glContext)
    {
#ifdef PS5_NATIVE_GPU
        glFinish();
        if (SDL_GL_MakeCurrent(this->window, nullptr) != 0)
        {
            // Keep presentation storage owned until native process exit when
            // detachment fails; deleting an in-use EGL surface is unsafe.
            Logger::error("ps5 native: GL detach failed during exit: {}", SDL_GetError());
            this->nvgContext = nullptr;
            return;
        }
#endif
#ifdef PS5_NATIVE_GPU
        SDL_ClearError();
#endif
        SDL_GL_DeleteContext(this->glContext);
        this->glContext = nullptr;
#ifdef PS5_NATIVE_GPU
        if (*SDL_GetError())
        {
            Logger::error("ps5 native: GL context destruction failed: {}", SDL_GetError());
            this->nvgContext = nullptr;
            return;
        }
#endif
    }
#endif
#ifdef PS5_NATIVE_GPU
    SDL_ClearError();
#endif
    SDL_DestroyWindow(this->window);
#ifdef PS5_NATIVE_GPU
    this->window = nullptr;
    this->nvgContext = nullptr;
#endif
#ifdef PS5_NATIVE_GPU
    if (*SDL_GetError())
    {
        Logger::error("ps5 native: window destruction failed: {}", SDL_GetError());
        return;
    }
    SDL_ClearError();
#endif
    SDL_Quit();
#ifdef PS5_NATIVE_GPU
    if (*SDL_GetError())
        Logger::error("ps5 native: SDL shutdown failed: {}", SDL_GetError());
    else
        Logger::info("ps5 native: EGL window/context and SDL shutdown completed");
#ifdef PS5_NATIVE_HDR
    // Only after complete SDL/EGL teardown; do not switch a live VideoOut.
    // This resets our runtime state. It does not claim HDMI SDR restoration
    // inside an HDR-enabled title (the title may retain an HDR carrier in SDR phases).
    if (!*SDL_GetError() && hdrSelected && ps5ExperimentalSelectHdrScanout(0) == 0)
        hdrSelected = false;
#endif
#endif
}

NVGcontext* SDLVideoContext::getNVGContext()
{
    return this->nvgContext;
}

SDL_Window* SDLVideoContext::getSDLWindow()
{
    return this->window;
}

void SDLVideoContext::fullScreen(bool fs)
{
#ifdef PS5_NATIVE_GPU
    // Fixed scanout is the native window contract; there is no windowed mode.
    (void)fs;
#else
#ifdef __WINRT__
    // win32 会很模糊，而且点击事件貌似也错位了，只给 winrt 使用。
    static unsigned int flag = SDL_WINDOW_FULLSCREEN;
#else
    static unsigned int flag = SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif
    SDL_SetWindowFullscreen(this->window, fs ? flag : 0);
#endif
}


} // namespace brls

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

#pragma once

#include <nanovg.h>

#include <cmath>
#ifdef PS5_NATIVE_GPU
#include <functional>
#include <utility>
#endif

// A VideoContext is responsible for providing a nanovg context for the app
// (so by extension it manages all the graphics state as well as the window / context).
// The VideoContext implementation must also provide the nanovg implementation. As such, there
// can only be one VideoContext linked at any time in the binary.
// Context creation and teardown can be done in the constructor and destructor.
class VideoContext
{
  public:
    virtual ~VideoContext() = default;

    /**
     * Called at the beginning of every frame to clear the window
     */
    virtual void clear(NVGcolor color) = 0;

    /**
     * Called at the beginning of every frame to begin it.
     */
    virtual void beginFrame() = 0;

    /**
     * Called at the end of every frame to end it (swap buffers...).
     */
    virtual void endFrame() = 0;
#ifdef PS5_NATIVE_GPU
    bool isRenderAvailable() const { return renderAvailable; }
    void invalidateRender() { renderAvailable = false; }
#endif
#ifdef PS5_NATIVE_HDR
    // Explicit owner handoff to embedded video. The currently bound GL FBO
    // may belong to another renderer and is not an ownership contract.
    virtual uint32_t getLinearHdrFramebuffer() const { return 0; }
    // embedded video render target for this frame. Sets framebuffer and
    // its internal format; false when no HDR target is available.
    virtual bool selectHdrVideoTarget(uint32_t& framebuffer, int& internalFormat) { return false; }
    // Immediately before the UI batch is submitted (after all views drew).
    virtual void hdrBeforeUiFlush(NVGcontext* vg) { }
#endif

    // Called on the video thread after a successful swap. Used by embedded
    // players to report presentation after the platform has submitted the frame.
#ifdef PS5_NATIVE_GPU
    void setPresentedCallback(std::function<void()> callback) { presentedCallback = std::move(callback); }
#endif

    /**
     * Can be called by the application to reset the graphics
     * state, in case there is a need to use the graphics API
     * directly (for instance direct OpenGL calls).
     */
    virtual void resetState() = 0;

    /*
     * scale factor between window size and drawable size
     */
    virtual double getScaleFactor() = 0;

    /*
     * Full screen window at primary monitor
     */
    virtual void fullScreen(bool fs) { }

    virtual NVGcontext* getNVGContext() = 0;

    virtual void setSwapInterval(int interval) = 0;

    virtual int getCurrentMonitorIndex() { return 0; }

    static inline bool FULLSCREEN = false;

    // The position and size of the window in the last non-full-screen mode
    static inline float posX = NAN, posY = NAN;
    static inline uint32_t sizeW = 0, sizeH = 0;

    static inline int monitorIndex = 0;

    static inline int swapInterval = 1;

#ifdef PS5_NATIVE_GPU
  protected:
    void notifyPresented() { if (presentedCallback) presentedCallback(); }

  private:
    bool renderAvailable = true;
    std::function<void()> presentedCallback;
#endif
};

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

#include <SDL2/SDL.h>

#include <borealis/core/video.hpp>
#ifdef PS5_NATIVE_HDR
#include <memory>
namespace ps5_native_hdr { class Frame; }
#endif

namespace brls
{

// SDL Video Context
class SDLVideoContext : public VideoContext
{
  public:
    SDLVideoContext(std::string windowTitle, uint32_t windowWidth, uint32_t windowHeight, float windowXPos, float windowYPos);
    ~SDLVideoContext() override;

    NVGcontext* getNVGContext() override;

    void clear(NVGcolor color) override;
    void beginFrame() override;
    void endFrame() override;

    void setSwapInterval(int interval) override;
    void resetState() override;
    void fullScreen(bool fs) override;

    SDL_Window* getSDLWindow();

    double getScaleFactor() override;
#ifdef PS5_NATIVE_HDR
    uint32_t getLinearHdrFramebuffer() const override;
    bool selectHdrVideoTarget(uint32_t& framebuffer, int& internalFormat) override;
    void hdrBeforeUiFlush(NVGcontext* vg) override;
#endif

  private:
#ifdef PS5_NATIVE_GPU
    void cleanup();
#endif
    SDL_Window* window     = nullptr;
#ifdef PS5_NATIVE_GPU
    SDL_GLContext glContext = nullptr;
#endif
    NVGcontext* nvgContext = nullptr;
#ifdef PS5_NATIVE_HDR
    std::unique_ptr<ps5_native_hdr::Frame> hdrFrame;
    bool hdrSelected = false;
    NVGcolor hdrClearColor{};
    bool hdrClearNow = true;
#endif
};

} // namespace brls

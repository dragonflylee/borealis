/*
    Copyright 2026 Daniil Vinogradov (XITRIX)

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
#ifdef __SWITCH2__

#include <memory>

#include <borealis/core/video.hpp>

namespace brls
{

class Switch2VulkanContext;

// Native Switch 2 video context.
//
// Unlike SDL2/GLFW, this owns no window object: libnx exposes a single default
// nwindow that libounce/OunceVK presents to, so the "window" is the console's
// display surface itself. The Vulkan context (Switch2VulkanContext) owns the
// swapchain, the NanoVG Vulkan renderer and the presentation loop.
class Switch2VideoContext : public VideoContext
{
  public:
    Switch2VideoContext(std::string windowTitle, uint32_t windowWidth, uint32_t windowHeight, float windowXPos, float windowYPos);
    ~Switch2VideoContext() override;

    NVGcontext* getNVGContext() override;

    void clear(NVGcolor color) override;
    void beginFrame() override;
    void endFrame() override;
    void setSwapInterval(int interval) override;
    void resetState() override;
    void fullScreen(bool fs) override;
    void waitIdle() override;

    double getScaleFactor() override;

    // The Vulkan context owning the swapchain and the NanoVG renderer.
    Switch2VulkanContext* getVulkanContext();

  private:
    NVGcontext* nvgContext = nullptr;
};

} // namespace brls

#endif

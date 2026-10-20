#pragma once

#ifdef PS5_NATIVE_GPU
#include "ps5_native_display_config.hpp"

namespace brls::ps5_native_display
{
// Build-time rendering dimensions, not negotiated HDMI or playback capability.
inline constexpr int width{PS5_NATIVE_DISPLAY_WIDTH};
inline constexpr int height{PS5_NATIVE_DISPLAY_HEIGHT};
inline constexpr int refreshHz{PS5_NATIVE_DISPLAY_REFRESH_HZ};
static_assert(refreshHz == 60 &&
    ((width == 1920 && height == 1080) || (width == 3840 && height == 2160)),
    "Unsupported native display configuration");

struct Dimensions
{
    int windowWidth = 0;
    int windowHeight = 0;
    int drawableWidth = 0;
    int drawableHeight = 0;
};

// SDL size queries return void. Clear every output before querying so a failed
// or partial query cannot reuse an earlier valid dimension. Publication occurs
// only after the window and drawable both satisfy the immutable contract.
template<class WindowQuery, class DrawableQuery, class Apply>
bool queryAndApply(Dimensions& observed, WindowQuery windowQuery,
    DrawableQuery drawableQuery, Apply apply)
{
    observed = {};
    windowQuery(&observed.windowWidth, &observed.windowHeight);
    drawableQuery(&observed.drawableWidth, &observed.drawableHeight);
    if (observed.windowWidth <= 0 || observed.windowHeight <= 0 ||
        observed.drawableWidth <= 0 || observed.drawableHeight <= 0 ||
        observed.windowWidth != width || observed.windowHeight != height ||
        observed.drawableWidth != width || observed.drawableHeight != height)
        return false;
    apply(observed);
    return true;
}
} // namespace brls::ps5_native_display
#endif

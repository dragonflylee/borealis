#pragma once

#include <cmath>
#include <cstdint>

// per-frame HDR composition mode for embedded PQ video.
//
// mpv writes PQ BT.2020 (target-peak 10000). Frames that draw no video keep the
// linear RGBA16F UI target and its PQ conversion (Menu). Video frames either
//   - Direct: mpv renders straight into the 10-bit window; no UI clear, no
//     composition and no conversion pass. Predicted from the previous frame,
//     which must have drawn video and queued no UI draws.
//   - Composite: mpv renders into a 10-bit video texture; UI draws into the
//     linear target cleared transparent; one pass blends UI over decoded video
//     in linear light and encodes PQ.
//   - DirectFallback: a predicted direct frame that nonetheless queued UI.
//     The UI is drawn into the transparent linear target and blended over the
//     window in PQ space for that one frame; the next frame is Composite.
// The decision uses only information available before GL work it controls.
// DirectBounded: a video frame whose queued UI covers a small rectangle
// (the playback progress bar) keeps Direct. Only that rectangle of the linear
// target is cleared and blended over the window in PQ space. Large UI still
// leads to Composite, so translucent overlays keep linear-light blending.
namespace ps5_native_hdr {

// DirectWindow: bounded UI that needs no stencil is drawn straight into the
// ten-bit window with PQ output, so no linear target, clear or blend pass runs.
enum class FrameMode : unsigned { Menu, Composite, Direct, DirectFallback, DirectBounded, DirectWindow };
enum class UiPreparation : unsigned { None, OpaqueClear, TransparentClear, BoundedClear, WindowDirect };

// GL window pixels, origin bottom-left.
struct UiRegion {
    unsigned x = 0, y = 0, width = 0, height = 0;
};
// The GPU clear floor of the runtime (PS5_GPU_CLEAR_MIN_PIXELS); smaller
// scissored clears take a CPU path that flushes the whole target.
constexpr uint64_t minimumRegionPixels = 16384;
constexpr unsigned regionPadding = 2;
// NanoVG vertex bounds (view units, y down) to a padded pixel rectangle. True
// when the rectangle is at most one eighth of the target.
inline bool boundedUiRegion(float minX, float minY, float maxX, float maxY, float viewWidth, float viewHeight,
                            unsigned width, unsigned height, UiRegion& out) {
    if (!std::isfinite(minX) || !std::isfinite(minY) || !std::isfinite(maxX) || !std::isfinite(maxY) ||
        !std::isfinite(viewWidth) || !std::isfinite(viewHeight) || viewWidth <= 0 || viewHeight <= 0 ||
        !width || !height || minX > maxX || minY > maxY)
        return false;
    const auto scale = [](float value, float view, unsigned size, bool up) {
        const double pixel = double(value) / view * size;
        const double rounded = up ? std::ceil(pixel) : std::floor(pixel);
        return rounded < 0 ? 0.0 : rounded > size ? double(size) : rounded;
    };
    const auto pad = [](double value, int delta, unsigned size) {
        const double padded = value + delta;
        return unsigned(padded < 0 ? 0 : padded > size ? size : padded);
    };
    unsigned left = pad(scale(minX, viewWidth, width, false), -int(regionPadding), width);
    unsigned right = pad(scale(maxX, viewWidth, width, true), int(regionPadding), width);
    unsigned top = pad(scale(minY, viewHeight, height, false), -int(regionPadding), height);
    unsigned bottom = pad(scale(maxY, viewHeight, height, true), int(regionPadding), height);
    // Grow symmetrically, clamped, until the GPU clear floor is met.
    const auto grow = [](unsigned& low, unsigned& high, unsigned size, uint64_t wanted) {
        while (high - low < wanted && (low > 0 || high < size)) {
            if (low > 0) --low;
            if (high - low < wanted && high < size) ++high;
        }
    };
    if (right == left) grow(left, right, width, 1);
    if (bottom == top) grow(top, bottom, height, 1);
    const uint64_t neededHeight = (minimumRegionPixels + (right - left) - 1) / (right - left);
    grow(top, bottom, height, neededHeight);
    if (uint64_t(right - left) * (bottom - top) < minimumRegionPixels)
        grow(left, right, width, (minimumRegionPixels + (bottom - top) - 1) / (bottom - top));
    out = {left, height - bottom, right - left, bottom - top};
    return uint64_t(out.width) * out.height >= minimumRegionPixels &&
           uint64_t(out.width) * out.height <= uint64_t(width) * height / 8;
}

class ModeState {
public:
    // Start of a frame. Returns true when the opaque UI clear must run now.
    bool beginFrame() {
        video = false;
        bounded = false;
        window = false;
        direct = previousVideo && !previousUi;
        // A likely video frame defers the clear until its target is known.
        cleared = !previousVideo;
        return cleared;
    }
    // Embedded video is about to render. Returns true for Direct (window
    // target). For Composite the caller clears the linear target transparent.
    bool videoTarget() {
        video = true;
        if (direct) return true;
        cleared = true;
        return false;
    }
    // Immediately before NanoVG submits this frame's queued draws.
    UiPreparation beforeUiFlush(bool uiQueued) {
        ui = uiQueued;
        if (!video) {
            mode = FrameMode::Menu;
            if (cleared) return UiPreparation::None;
            cleared = true;
            return UiPreparation::OpaqueClear;
        }
        if (!direct) {
            mode = FrameMode::Composite;
            return UiPreparation::None;
        }
        if (!uiQueued) {
            mode = FrameMode::Direct;
            return UiPreparation::None;
        }
        if (bounded) {
            if (window) {
                mode = FrameMode::DirectWindow;
                return UiPreparation::WindowDirect;
            }
            mode = FrameMode::DirectBounded;
            cleared = true;
            return UiPreparation::BoundedClear;
        }
        mode = FrameMode::DirectFallback;
        cleared = true;
        return UiPreparation::TransparentClear;
    }
    // As above, with whether the queued UI fits a bounded region. Small UI does
    // not prevent the next frame from rendering video directly.
    UiPreparation beforeUiFlush(bool uiQueued, bool small) {
        bounded = uiQueued && small;
        window = false;
        return beforeUiFlush(uiQueued);
    }
    // As above, with whether the queued UI can be drawn straight into the window
    // (small, and no call needs the stencil).
    UiPreparation beforeUiFlush(bool uiQueued, bool small, bool windowSafe) {
        bounded = uiQueued && small;
        window = bounded && windowSafe;
        return beforeUiFlush(uiQueued);
    }
    bool videoFrame() const { return video; }
    // After presentation, record the prediction inputs for the next frame.
    FrameMode endFrame() {
        previousVideo = video;
        previousUi = ui && !bounded;
        const FrameMode result = mode;
        mode = FrameMode::Menu;
        return result;
    }
    // Any presentation failure or owner reset returns to the conservative path.
    void reset() { previousVideo = previousUi = video = ui = direct = bounded = window = false; cleared = true; mode = FrameMode::Menu; }
    FrameMode pending() const { return mode; }

private:
    bool previousVideo = false, previousUi = false;
    bool video = false, ui = false, direct = false, cleared = true;
    bool bounded = false;
    bool window = false;
    FrameMode mode = FrameMode::Menu;
};

} // namespace ps5_native_hdr

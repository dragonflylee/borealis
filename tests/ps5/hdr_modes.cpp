#include <borealis/platforms/ps5/native_hdr_mode.hpp>
#include <cassert>
#include <cmath>
#include <limits>
#include <initializer_list>

int main()
{
    using namespace ps5_native_hdr;
    ModeState state;
    assert(state.beginFrame());
    assert(!state.videoTarget());
    assert(state.beforeUiFlush(false, false, false) == UiPreparation::None);
    assert(state.endFrame() == FrameMode::Composite);
    assert(!state.beginFrame());
    assert(state.videoTarget());
    assert(state.beforeUiFlush(true, true, true) == UiPreparation::WindowDirect);
    assert(state.endFrame() == FrameMode::DirectWindow);
    assert(!state.beginFrame());
    assert(state.videoTarget());
    assert(state.beforeUiFlush(true, true, false) == UiPreparation::BoundedClear);
    assert(state.endFrame() == FrameMode::DirectBounded);
    assert(!state.beginFrame());
    assert(state.videoTarget());
    assert(state.beforeUiFlush(true, false, false) == UiPreparation::TransparentClear);
    assert(state.endFrame() == FrameMode::DirectFallback);
    assert(!state.beginFrame());
    assert(!state.videoTarget());
    assert(state.beforeUiFlush(true, false, false) == UiPreparation::None);
    assert(state.endFrame() == FrameMode::Composite);
    assert(!state.beginFrame());
    assert(state.beforeUiFlush(true, false, false) == UiPreparation::OpaqueClear);
    assert(state.endFrame() == FrameMode::Menu);
    state.reset();
    assert(state.beginFrame());
    assert(!state.videoTarget());

    UiRegion region;
    assert(boundedUiRegion(100, 100, 200, 120, 1920, 1080, 3840, 2160, region));
    assert(region.width * uint64_t(region.height) >= minimumRegionPixels);
    assert(region.x + region.width <= 3840 && region.y + region.height <= 2160);
    assert(!boundedUiRegion(0, 0, 1920, 1080, 1920, 1080, 3840, 2160, region));
    assert(!boundedUiRegion(0, 0, 10, 10, 0, 1080, 3840, 2160, region));
    assert(!boundedUiRegion(std::numeric_limits<float>::quiet_NaN(), 0, 10, 10,
                            1920, 1080, 3840, 2160, region));
}

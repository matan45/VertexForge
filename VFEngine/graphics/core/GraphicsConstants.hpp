#pragma once

#include <cstdint>

namespace core
{
    constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    // Upper bound on swapchain images (minImageCount + 1, see SwapChain.cpp). Per-image
    // resources (e.g. thread-pool shadow secondaries) are indexed by the acquired image
    // index and guarded by per-image fences, so they must be sized to the image count, NOT
    // to MAX_FRAMES_IN_FLIGHT (which is smaller and would alias distinct in-flight images).
    constexpr uint32_t MAX_SWAPCHAIN_IMAGES = 8;
}

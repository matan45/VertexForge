#include "ResolutionManager.hpp"
#include <algorithm>

namespace render::upscaling
{
    void ResolutionManager::setDisplayResolution(uint32_t width, uint32_t height)
    {
        displayResolution = vk::Extent2D{width, height};
        recompute();
    }

    void ResolutionManager::setQualityMode(::postprocess::UpscaleQuality quality)
    {
        currentQuality = quality;
        recompute();
    }

    void ResolutionManager::setDynamicScale(float scale, float minScale)
    {
        // Guard against an inverted/out-of-range band, then clamp the requested scale into it.
        // maxScale is fixed at 1.0 (== the preset's nominal render size).
        float lo = std::clamp(minScale, 0.05f, 1.0f);
        dynamicScale = std::clamp(scale, lo, 1.0f);
        recompute();
    }

    void ResolutionManager::recompute()
    {
        scaleFactor = ::postprocess::UpscaleSettings::getScaleFactor(currentQuality);

        // VK-1531: fold the dynamic multiplier in. dynamicScale == 1.0f reproduces the previous
        // (display / scaleFactor) result exactly (IEEE x * 1.0f == x), so the non-adaptive path is
        // byte-identical. dynamicScale <= 1 and scaleFactor >= 1 keep renderRes <= display.
        float dyn = std::clamp(dynamicScale, 0.05f, 1.0f);
        uint32_t renderW = std::max(1u, static_cast<uint32_t>(
            static_cast<float>(displayResolution.width) * dyn / scaleFactor));
        uint32_t renderH = std::max(1u, static_cast<uint32_t>(
            static_cast<float>(displayResolution.height) * dyn / scaleFactor));

        // Ensure even dimensions (required by many upscalers)
        renderW = (renderW + 1u) & ~1u;
        renderH = (renderH + 1u) & ~1u;

        renderResolution = vk::Extent2D{renderW, renderH};
    }
}

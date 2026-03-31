#include "ResolutionManager.hpp"
#include <algorithm>

namespace render::upscaling
{
    void ResolutionManager::setDisplayResolution(uint32_t width, uint32_t height)
    {
        displayResolution = vk::Extent2D{width, height};
        recompute();
    }

    void ResolutionManager::setQualityMode(postprocess::UpscaleQuality quality)
    {
        currentQuality = quality;
        recompute();
    }

    void ResolutionManager::recompute()
    {
        scaleFactor = postprocess::UpscaleSettings::getScaleFactor(currentQuality);

        uint32_t renderW = std::max(1u, static_cast<uint32_t>(
            static_cast<float>(displayResolution.width) / scaleFactor));
        uint32_t renderH = std::max(1u, static_cast<uint32_t>(
            static_cast<float>(displayResolution.height) / scaleFactor));

        // Ensure even dimensions (required by many upscalers)
        renderW = (renderW + 1u) & ~1u;
        renderH = (renderH + 1u) & ~1u;

        renderResolution = vk::Extent2D{renderW, renderH};
    }
}

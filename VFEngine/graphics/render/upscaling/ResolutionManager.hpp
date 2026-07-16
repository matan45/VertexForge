#pragma once

#include "../../../utilities/postprocess/PostProcessTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <cstdint>

namespace render::upscaling
{
    /// Manages the split between internal render resolution and display resolution.
    /// When upscaling is active, the engine renders at a lower internal resolution
    /// and the upscaler reconstructs the full display resolution output.
    class ResolutionManager
    {
    public:
        void setDisplayResolution(uint32_t width, uint32_t height);
        void setQualityMode(::postprocess::UpscaleQuality quality);

        // VK-1531: continuous dynamic-resolution multiplier layered UNDER the quality preset.
        // scale is clamped to [minScale, 1.0]; 1.0 == the preset's nominal render size.
        void setDynamicScale(float scale, float minScale);

        vk::Extent2D getDisplayResolution() const { return displayResolution; }
        vk::Extent2D getRenderResolution() const { return renderResolution; }

        uint32_t getRenderWidth() const { return renderResolution.width; }
        uint32_t getRenderHeight() const { return renderResolution.height; }
        uint32_t getDisplayWidth() const { return displayResolution.width; }
        uint32_t getDisplayHeight() const { return displayResolution.height; }

        float getScaleFactor() const { return scaleFactor; }
        bool isUpscaling() const { return scaleFactor > 1.0f; }
        float getDynamicScale() const { return dynamicScale; }

        ::postprocess::UpscaleQuality getQualityMode() const { return currentQuality; }

    private:
        vk::Extent2D displayResolution{1920, 1080};
        vk::Extent2D renderResolution{1920, 1080};
        float scaleFactor = 1.0f;
        float dynamicScale = 1.0f; // VK-1531: adaptive multiplier in (0, 1]
        ::postprocess::UpscaleQuality currentQuality = ::postprocess::UpscaleQuality::Native;

        void recompute();
    };
}

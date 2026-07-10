#pragma once

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace render::gpudriven
{
    struct TerrainRVTLayout
    {
        std::vector<vk::Format> planeFormats;
        uint32_t bytesPerTexel = 0;
    };

    // Shared RVT storage contract. Detail maps add a tangent-normal RGBA8 plane and
    // a linear HDR-emission RGBA16F plane to the legacy albedo/ORM pair.
    [[nodiscard]] inline TerrainRVTLayout terrainRVTLayout(bool detailMaps)
    {
        if (!detailMaps)
        {
            return {{vk::Format::eR8G8B8A8Srgb,
                     vk::Format::eR8G8B8A8Unorm},
                    8u};
        }

        return {{vk::Format::eR8G8B8A8Srgb,
                 vk::Format::eR8G8B8A8Unorm,
                 vk::Format::eR8G8B8A8Unorm,
                 vk::Format::eR16G16B16A16Sfloat},
                20u};
    }
}

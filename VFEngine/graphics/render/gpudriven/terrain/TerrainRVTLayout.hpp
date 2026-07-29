#pragma once

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "terrain/TerrainRVTBudget.hpp"
#include "../../virtualtexture/VTTypes.hpp"

namespace render::gpudriven
{
    struct TerrainRVTLayout
    {
        std::vector<vk::Format> planeFormats;
        uint32_t bytesPerTexel = 0;
    };

    // Shared RVT storage contract. Detail maps add a tangent-normal RGBA8 plane and
    // a linear HDR-emission RGBA16F plane to the legacy albedo/ORM pair.
    //
    // VK-1610: the plane count and byte cost live in utilities (terrain/TerrainRVTBudget.hpp)
    // so the Editor's render-config UI can show what a pool budget actually buys without
    // including a graphics header. The asserts below are what keep the two definitions — and
    // the pool-sizing math in VTTypes.hpp — from drifting apart.
    static_assert(::terrain::TERRAIN_RVT_PAGE_SIZE == vt::VT_PAGE_SIZE,
                  "TerrainRVTBudget page size must mirror render::vt::VT_PAGE_SIZE");
    static_assert(::terrain::TERRAIN_RVT_MAX_POOL_DIM == vt::VT_MAX_POOL_DIM,
                  "TerrainRVTBudget pool-dim cap must mirror render::vt::VT_MAX_POOL_DIM");

    [[nodiscard]] inline TerrainRVTLayout terrainRVTLayout(bool detailMaps)
    {
        if (!detailMaps)
        {
            return {{vk::Format::eR8G8B8A8Srgb,
                     vk::Format::eR8G8B8A8Unorm},
                    ::terrain::terrainRVTBytesPerTexel(false)};
        }

        return {{vk::Format::eR8G8B8A8Srgb,
                 vk::Format::eR8G8B8A8Unorm,
                 vk::Format::eR8G8B8A8Unorm,
                 vk::Format::eR16G16B16A16Sfloat},
                ::terrain::terrainRVTBytesPerTexel(true)};
    }
}

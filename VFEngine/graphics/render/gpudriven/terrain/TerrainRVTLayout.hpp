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

    // VK-1620 world-height plane. R16_UNORM, matching UE5's World Height runtime virtual texture.
    // 16 bits of unorm over the terrain's authored height range is 1.68 mm on the default -10..100
    // span — fine enough that the terrain normal reconstructed from this plane's gradient terraces
    // by under a degree at the mip-0 texel size.
    inline constexpr vk::Format TERRAIN_RVT_WORLD_HEIGHT_FORMAT = vk::Format::eR16Unorm;

    // R16_UNORM is NOT in Vulkan's mandatory format-feature table, and VTPhysicalPool creates its
    // planes without any capability check — an unsupported format would fail image creation deep
    // inside pool init. Ask first, and let the caller turn the feature off rather than carrying a
    // second byte count through the budget math (which would desync the Editor's page-count readout
    // from the pool the engine actually builds).
    //
    // The bake renders into this plane, the blend samples it with bilinear filtering, and the pool
    // is cleared through a transfer, so all four features are genuinely required.
    [[nodiscard]] inline bool terrainRVTWorldHeightSupported(vk::PhysicalDevice physicalDevice)
    {
        constexpr vk::FormatFeatureFlags required =
            vk::FormatFeatureFlagBits::eColorAttachment |
            vk::FormatFeatureFlagBits::eSampledImage |
            vk::FormatFeatureFlagBits::eSampledImageFilterLinear |
            vk::FormatFeatureFlagBits::eTransferDst;

        const vk::FormatProperties props = physicalDevice.getFormatProperties(TERRAIN_RVT_WORLD_HEIGHT_FORMAT);
        return (props.optimalTilingFeatures & required) == required;
    }

    // The world-height plane is appended LAST so plane indices 0-3 keep their meaning across all
    // four combinations — mesh_terrain.glsl's set-5 bindings and terrain_rvt_bake.glsl's MRT
    // locations are index-addressed, so inserting anywhere else would silently re-point them.
    [[nodiscard]] inline TerrainRVTLayout terrainRVTLayout(bool detailMaps, bool worldHeight)
    {
        TerrainRVTLayout layout{};
        layout.planeFormats = {vk::Format::eR8G8B8A8Srgb,
                               vk::Format::eR8G8B8A8Unorm};

        if (detailMaps)
        {
            layout.planeFormats.push_back(vk::Format::eR8G8B8A8Unorm);
            layout.planeFormats.push_back(vk::Format::eR16G16B16A16Sfloat);
        }

        if (worldHeight)
            layout.planeFormats.push_back(TERRAIN_RVT_WORLD_HEIGHT_FORMAT);

        layout.bytesPerTexel = ::terrain::terrainRVTBytesPerTexel(detailMaps, worldHeight);
        return layout;
    }

    // MRT location of the world-height plane, which depends on whether the detail planes are
    // present. Both the baker (blend-attachment count) and the bake shader's `#define` need it, and
    // getting the two out of step writes height into the emission plane.
    [[nodiscard]] inline constexpr uint32_t terrainRVTWorldHeightPlaneIndex(bool detailMaps) noexcept
    {
        return detailMaps ? ::terrain::TERRAIN_RVT_DETAIL_PLANES : ::terrain::TERRAIN_RVT_LEGACY_PLANES;
    }
}

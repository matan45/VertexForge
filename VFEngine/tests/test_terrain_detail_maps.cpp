#include <doctest.h>

#include <terrain/TerrainRVTBudget.hpp>
#include <terrain/TerrainMaterialTypes.hpp>
#include <render/virtualtexture/VTTypes.hpp>
#include <render/gpudriven/terrain/TerrainRVTLayout.hpp>
#include <render/gpudriven/terrain/TerrainLayerPBRResolver.hpp>
#include <render/material/MaterialPBRExtractor.hpp>

#include <vector>

// ============================================================
// VK-1610 - terrain per-layer detail maps: content-derived permutation + RVT pool geometry.
//
// The decision this story encodes is that TERRAIN_DETAIL_MAPS should follow the terrain MATERIAL,
// not a bare setting. The reason is asymmetric cost: the live composite already skips the normal
// fetch for a layer with no normal map (it takes a wave-uniform branch on normalTextureIndex), but
// the RVT page pool does not - the four-plane layout costs 20 bytes per texel instead of 8 whether
// or not a single layer uses it, which at the shipped 128 MB budget is 400 resident pages instead
// of 1024. Turning the permutation on for terrain that gains nothing from it buys a 61% cut in
// resident pages for zero visual difference.
//
// Both halves of that are pure and CPU-testable, so they are pinned here rather than left to a
// GPU sign-off: the predicate that reads the resolved material, and the page-count math the editor
// UI and the manager's warning both quote.
// ============================================================

using render::gpudriven::ResolvedTerrainLayerPBR;
using render::gpudriven::resolveTerrainLayerPBR;
using render::gpudriven::terrainMaterialWantsDetailMaps;

namespace
{
    ResolvedTerrainLayerPBR layerWith(const char* normalPath, const char* emissionPath)
    {
        ResolvedTerrainLayerPBR l;
        l.albedoPath = "grass_albedo.vfImage";
        l.ormPath = "grass_orm.vfImage";
        l.normalPath = normalPath;
        l.emissionPath = emissionPath;
        return l;
    }
}

TEST_CASE("VK-1610 detail-map derivation: a material with no normal or emission maps wants none")
{
    SUBCASE("empty material")
    {
        CHECK_FALSE(terrainMaterialWantsDetailMaps({}));
    }

    SUBCASE("layers with albedo + ORM only")
    {
        const std::vector<ResolvedTerrainLayerPBR> layers{
            layerWith("", ""), layerWith("", ""), layerWith("", "")};
        CHECK_FALSE(terrainMaterialWantsDetailMaps(layers));
    }

    SUBCASE("a single layer with a normal map is enough")
    {
        // The permutation is per-material, not per-layer: the composite is one shader for all 8
        // splat channels, so one authored normal map turns it on for the whole terrain.
        const std::vector<ResolvedTerrainLayerPBR> layers{
            layerWith("", ""), layerWith("rock_normal.vfImage", ""), layerWith("", "")};
        CHECK(terrainMaterialWantsDetailMaps(layers));
    }

    SUBCASE("emission alone also counts")
    {
        // The detail permutation is what carries per-layer emission COLOUR; without it emission is
        // a scalar multiplied into albedo, so an emission texture would silently do nothing.
        const std::vector<ResolvedTerrainLayerPBR> layers{layerWith("", "lava_emissive.vfImage")};
        CHECK(terrainMaterialWantsDetailMaps(layers));
    }
}

TEST_CASE("VK-1610 detail-map derivation reads RESOLVED paths, so a failed material wants nothing")
{
    // resolveTerrainLayerPBR is the seam where a material reference becomes texture paths. A layer
    // whose material failed to extract resolves to empty paths, and must therefore NOT drag the
    // whole terrain into the four-plane RVT layout to bind textures that do not exist.
    terrain::TerrainMaterialLayer layer;
    layer.tilingScale = 2.0f;

    const ResolvedTerrainLayerPBR failed = resolveTerrainLayerPBR(layer, nullptr);
    CHECK(failed.normalPath.empty());
    CHECK(failed.emissionPath.empty());
    CHECK_FALSE(terrainMaterialWantsDetailMaps({failed}));

    render::mesh::ExtractedPBRValues pbr;
    pbr.materialPath = "rock.vfMat";
    pbr.albedoTexturePath = "rock_albedo.vfImage";
    pbr.normalTexturePath = "rock_normal.vfImage";

    const ResolvedTerrainLayerPBR ok = resolveTerrainLayerPBR(layer, &pbr);
    CHECK(ok.normalPath == "rock_normal.vfImage");
    CHECK(terrainMaterialWantsDetailMaps({ok}));
}

TEST_CASE("VK-1610 RVT pool geometry: the same budget buys very different page counts")
{
    // These two numbers ARE the story. 128 MB is the shipped default.
    // worldHeight is held false throughout this file: it is VK-1620's axis and is covered by
    // test_terrain_rvt_world_height.cpp. What is pinned here is that adding it did not disturb
    // the detail-maps numbers this story exists to defend.
    const auto plain = terrain::terrainRVTPoolGeometry(128, false, false);
    CHECK(plain.planeCount == 2);
    CHECK(plain.bytesPerTexel == 8);
    CHECK(plain.poolDim == 4096);
    CHECK(plain.capacityPages == 1024); // 32x32 tiles of 128 texels
    CHECK(plain.bytes == (128ull << 20));

    const auto detail = terrain::terrainRVTPoolGeometry(128, true, false);
    CHECK(detail.planeCount == 4);
    CHECK(detail.bytesPerTexel == 20);
    CHECK(detail.poolDim == 2560);
    CHECK(detail.capacityPages == 400); // 20x20 - a 61% cut at the same MB
    CHECK(detail.bytes <= (128ull << 20));

    // Restoring the legacy page count under the detail layout is what the budget would have to be
    // raised to; recorded here so the recommendation in the UI warning is not a guess. 320 MiB is
    // EXACT, not approximate: 4096^2 x 20 B = 335,544,320 = 320 << 20.
    CHECK(terrain::terrainRVTPoolGeometry(320, true, false).capacityPages == 1024);
    CHECK(terrain::terrainRVTPoolGeometry(319, true, false).capacityPages < 1024);
}

TEST_CASE("VK-1610 bytesPerTexel is the true sum of the plane formats")
{
    // The cross-check below is deliberately NOT `layout.bytesPerTexel == terrainRVTBytesPerTexel()`
    // - that is now a tautology, because terrainRVTLayout() returns exactly that value. The thing
    // that can actually drift is the byte count against the FORMAT LIST: VK-1614 already has
    // reserved fields waiting, and adding a 5th plane while forgetting to bump
    // TERRAIN_RVT_DETAIL_BYTES_PER_TEXEL would silently oversize every pool by a plane.
    //
    // VK-1620 is exactly the event this predicted, and it landed as designed: the world-height
    // plane's R16_UNORM tripped the default branch until the switch below learned it. The sweep is
    // now over all FOUR (detail, worldHeight) combinations, because the two axes are independent
    // and only the cross product proves the byte count tracks the format list in every one.
    auto formatBytes = [](vk::Format f) -> uint32_t
    {
        switch (f)
        {
            case vk::Format::eR16Unorm:           return 2; // VK-1620 world-height plane
            case vk::Format::eR8G8B8A8Srgb:
            case vk::Format::eR8G8B8A8Unorm:      return 4;
            case vk::Format::eR16G16B16A16Sfloat: return 8;
            default:
                // Not FAIL(): doctest's FAIL expands to a bare `return`, which does not compile in
                // a lambda with a non-void return type.
                CHECK_MESSAGE(false, "unhandled terrain RVT plane format - add it to this switch");
                return 0;
        }
    };

    for (const bool detail : {false, true})
    {
        for (const bool worldHeight : {false, true})
        {
            const auto layout = render::gpudriven::terrainRVTLayout(detail, worldHeight);
            uint32_t summed = 0;
            for (const vk::Format f : layout.planeFormats)
                summed += formatBytes(f);
            CHECK(summed == layout.bytesPerTexel);
            CHECK(summed == terrain::terrainRVTBytesPerTexel(detail, worldHeight));
        }
    }
}

TEST_CASE("VK-1610 RVT pool geometry agrees with the graphics-side sizing math")
{
    // The editor UI cannot include graphics headers, so the page math is duplicated in utilities.
    // This is the check that keeps the duplicate honest - if either side changes, it fails here
    // rather than showing the user a page count the engine never builds.
    for (const bool detail : {false, true})
    {
        for (const bool worldHeight : {false, true})
        {
            const auto layout = render::gpudriven::terrainRVTLayout(detail, worldHeight);
            CHECK(static_cast<uint32_t>(layout.planeFormats.size())
                  == terrain::terrainRVTPlaneCount(detail, worldHeight));

            // The last two budgets are past VT_MAX_POOL_DIM, so the hardware clamp is exercised on
            // both sides too - the utilities mirror must saturate at the same edge the graphics math
            // does, not keep growing.
            for (const uint32_t budgetMB : {32u, 64u, 128u, 256u, 512u, 1024u, 8192u, 1048576u})
            {
                const auto geo = terrain::terrainRVTPoolGeometry(budgetMB, detail, worldHeight);
                const uint32_t graphicsDim =
                    render::vt::vtPoolDimForBudget(budgetMB, /*planes*/ 1, layout.bytesPerTexel);
                CHECK(geo.poolDim == graphicsDim);
                CHECK(geo.capacityPages == render::vt::vtMaxTiles(graphicsDim));
            }
        }
    }
}

TEST_CASE("VK-1610 RVT pool geometry clamps like the graphics math does")
{
    // Floor: a budget too small for one page still yields exactly one, never zero (a zero-tile
    // atlas would divide by zero in the utilization readout and create an unusable image).
    const auto tiny = terrain::terrainRVTPoolGeometry(0, true, false);
    CHECK(tiny.poolDim == terrain::TERRAIN_RVT_PAGE_SIZE);
    CHECK(tiny.capacityPages == 1);

    // Ceiling: the atlas edge never exceeds what maxImageDimension2D allows, so a huge budget
    // silently buys nothing past that point - the same behaviour vtPoolBudgetClamped warns about.
    const auto huge = terrain::terrainRVTPoolGeometry(4u * 1024u * 1024u, false, false);
    CHECK(huge.poolDim == terrain::TERRAIN_RVT_MAX_POOL_DIM);
}

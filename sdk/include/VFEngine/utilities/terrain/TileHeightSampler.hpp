#pragma once

#include "TerrainWeightMap.hpp"
#include <glm/glm.hpp>
#include <algorithm>
#include <cstdint>

namespace terrain
{
    // Direct, O(1) samplers over a single tile's raw arrays, for the procedural scatter
    // bake (VK-1581). They avoid the per-candidate GetTerrainHeightAtQuery round-trips the
    // interactive brush uses. Coordinates are tile-local (world units from the tile's min
    // corner). heightData is row-major z*vpt+x; vpt = vertices per side; the weightmap
    // shares this grid (resolution == vpt), so all three use the same world->grid transform:
    //   grid = clamp(local / vertexSpacing, 0, count-1),  vertexSpacing = worldTileSize/(vpt-1).

    inline float sampleTileHeightBilinear(const float* heightData, uint32_t vpt,
                                          float vertexSpacing, float localX, float localZ)
    {
        const float maxIdx = static_cast<float>(vpt - 1u);
        const float gx = std::clamp(localX / vertexSpacing, 0.0f, maxIdx);
        const float gz = std::clamp(localZ / vertexSpacing, 0.0f, maxIdx);
        const uint32_t x0 = static_cast<uint32_t>(gx);
        const uint32_t z0 = static_cast<uint32_t>(gz);
        const uint32_t x1 = std::min(x0 + 1u, vpt - 1u);
        const uint32_t z1 = std::min(z0 + 1u, vpt - 1u);
        const float fx = gx - static_cast<float>(x0);
        const float fz = gz - static_cast<float>(z0);
        const float h00 = heightData[z0 * vpt + x0];
        const float h10 = heightData[z0 * vpt + x1];
        const float h01 = heightData[z1 * vpt + x0];
        const float h11 = heightData[z1 * vpt + x1];
        const float hx0 = h00 + (h10 - h00) * fx;
        const float hx1 = h01 + (h11 - h01) * fx;
        return hx0 + (hx1 - hx0) * fz;
    }

    // Central-difference terrain normal over the bilinear height field (eps in world units).
    // Mirrors VegetationBrushServiceImpl::sampleTerrainNormal (n = {hL-hR, 2*eps, hD-hU}),
    // reading heightData directly instead of querying. At tile edges the samples clamp
    // inward, giving a slightly flattened edge normal — acceptable for the MVP bake.
    inline glm::vec3 sampleTileNormalCentralDiff(const float* heightData, uint32_t vpt,
                                                 float vertexSpacing, float localX, float localZ,
                                                 float eps)
    {
        const float hL = sampleTileHeightBilinear(heightData, vpt, vertexSpacing, localX - eps, localZ);
        const float hR = sampleTileHeightBilinear(heightData, vpt, vertexSpacing, localX + eps, localZ);
        const float hD = sampleTileHeightBilinear(heightData, vpt, vertexSpacing, localX, localZ - eps);
        const float hU = sampleTileHeightBilinear(heightData, vpt, vertexSpacing, localX, localZ + eps);
        return glm::normalize(glm::vec3(hL - hR, 2.0f * eps, hD - hU));
    }

    // Discrete curvature of the bilinear height field (VK-1585): the mean of the four axis
    // neighbours (at +/-eps) minus the centre height, in world-Y units. POSITIVE = concave
    // (centre sits below its neighbours, e.g. a hollow/valley), NEGATIVE = convex (a ridge/peak).
    // It is proportional to the 5-point discrete Laplacian.
    //
    // eps MUST be >= vertexSpacing. Bilinear interpolation is planar within a single grid cell
    // (its pure second derivatives are zero there), so a stencil narrower than one grid step
    // reads ~0 curvature everywhere; sampling a full vertexSpacing away lands on genuinely
    // different height values. At tile edges the neighbour samples clamp inward.
    inline float sampleTileCurvature(const float* heightData, uint32_t vpt, float vertexSpacing,
                                     float localX, float localZ, float eps)
    {
        const float hC = sampleTileHeightBilinear(heightData, vpt, vertexSpacing, localX, localZ);
        const float hL = sampleTileHeightBilinear(heightData, vpt, vertexSpacing, localX - eps, localZ);
        const float hR = sampleTileHeightBilinear(heightData, vpt, vertexSpacing, localX + eps, localZ);
        const float hD = sampleTileHeightBilinear(heightData, vpt, vertexSpacing, localX, localZ - eps);
        const float hU = sampleTileHeightBilinear(heightData, vpt, vertexSpacing, localX, localZ + eps);
        return (hL + hR + hD + hU) * 0.25f - hC;
    }

    // Bilinear splat weight of material palette layer `paletteLayer`. Returns 0 when the
    // layer is not present on this tile (findChannel == 0xFF). resolution == vpt.
    inline float sampleTileLayerWeightBilinear(const TileWeightMapData& wm, uint8_t paletteLayer,
                                               float localX, float localZ, float vertexSpacing)
    {
        const uint8_t channel = wm.findChannel(paletteLayer);
        if (channel == 0xFF || wm.resolution == 0)
            return 0.0f;
        const uint32_t res = wm.resolution;
        const float maxIdx = static_cast<float>(res - 1u);
        const float gx = std::clamp(localX / vertexSpacing, 0.0f, maxIdx);
        const float gz = std::clamp(localZ / vertexSpacing, 0.0f, maxIdx);
        const uint32_t x0 = static_cast<uint32_t>(gx);
        const uint32_t z0 = static_cast<uint32_t>(gz);
        const uint32_t x1 = std::min(x0 + 1u, res - 1u);
        const uint32_t z1 = std::min(z0 + 1u, res - 1u);
        const float fx = gx - static_cast<float>(x0);
        const float fz = gz - static_cast<float>(z0);
        const float w00 = wm.getWeight(channel, x0, z0);
        const float w10 = wm.getWeight(channel, x1, z0);
        const float w01 = wm.getWeight(channel, x0, z1);
        const float w11 = wm.getWeight(channel, x1, z1);
        const float wx0 = w00 + (w10 - w00) * fx;
        const float wx1 = w01 + (w11 - w01) * fx;
        return wx0 + (wx1 - wx0) * fz;
    }
}

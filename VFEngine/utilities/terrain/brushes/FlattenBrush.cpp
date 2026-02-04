#include "FlattenBrush.hpp"
#include "../TerrainTile.hpp"
#include "../BrushSampler.hpp"
#include <algorithm>
#include <cmath>

namespace terrain::brushes
{
    void FlattenBrush::apply(TerrainTile& tile, const BrushContext& context)
    {
        const auto& params = context.params;
        uint32_t vertexCount = tile.config.getVertexCount();
        float vertexSpacing = tile.config.getVertexSpacing();

        // Compute vertex AABB of brush influence on this tile
        float localMinX = context.brushCenter.x - params.radius - tile.worldOrigin.x;
        float localMinZ = context.brushCenter.y - params.radius - tile.worldOrigin.z;
        float localMaxX = context.brushCenter.x + params.radius - tile.worldOrigin.x;
        float localMaxZ = context.brushCenter.y + params.radius - tile.worldOrigin.z;

        uint32_t startX = static_cast<uint32_t>(std::max(0.0f, std::floor(localMinX / vertexSpacing)));
        uint32_t startZ = static_cast<uint32_t>(std::max(0.0f, std::floor(localMinZ / vertexSpacing)));
        uint32_t endX = std::min(vertexCount - 1, static_cast<uint32_t>(std::ceil(localMaxX / vertexSpacing)));
        uint32_t endZ = std::min(vertexCount - 1, static_cast<uint32_t>(std::ceil(localMaxZ / vertexSpacing)));

        for (uint32_t z = startZ; z <= endZ; ++z)
        {
            for (uint32_t x = startX; x <= endX; ++x)
            {
                float worldX = tile.worldOrigin.x + static_cast<float>(x) * vertexSpacing;
                float worldZ = tile.worldOrigin.z + static_cast<float>(z) * vertexSpacing;

                glm::vec2 delta(worldX - context.brushCenter.x, worldZ - context.brushCenter.y);

                float dist;
                if (params.shape == BrushShape::Circle)
                {
                    dist = glm::length(delta) / params.radius;
                }
                else
                {
                    dist = std::max(std::abs(delta.x), std::abs(delta.y)) / params.radius;
                }

                if (dist >= 1.0f)
                {
                    continue;
                }

                float influence = BrushSampler::applyFalloff(dist, params.falloff);
                float blendFactor = influence * params.strength * context.deltaTime;
                blendFactor = std::clamp(blendFactor, 0.0f, 1.0f);

                size_t idx = static_cast<size_t>(z) * vertexCount + x;
                float current = tile.heightData[idx];

                tile.heightData[idx] = std::clamp(
                    current + (context.targetHeight - current) * blendFactor,
                    tile.config.minHeight,
                    tile.config.maxHeight);
            }
        }
    }
}

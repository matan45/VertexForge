#include "RaiseLowerBrush.hpp"
#include "../TerrainTile.hpp"
#include "../BrushSampler.hpp"
#include <algorithm>
#include <cmath>

namespace terrain::brushes
{
    void RaiseLowerBrush::apply(TerrainTile& tile, const BrushContext& context)
    {
        const auto& params = context.params;
        uint32_t vertexCount = tile.config.getVertexCount();
        float vertexSpacing = tile.config.getVertexSpacing();
        float direction = context.invert ? -1.0f : 1.0f;

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
                float delta_h = direction * influence * params.strength * context.deltaTime;

                size_t idx = static_cast<size_t>(z) * vertexCount + x;
                tile.heightData[idx] = std::clamp(
                    tile.heightData[idx] + delta_h,
                    tile.config.minHeight,
                    tile.config.maxHeight);
            }
        }
    }
}

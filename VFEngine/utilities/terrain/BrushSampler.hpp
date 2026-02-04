#pragma once

#include "BrushTypes.hpp"
#include "TerrainTypes.hpp"
#include "brushes/ISculptBrush.hpp"
#include <vector>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace terrain
{
    class TerrainTile;

    struct BrushVertexInfo
    {
        size_t index;     // vertex buffer index
        float influence;  // falloff-adjusted influence [0, 1]
        float worldX;     // world-space X position
        float worldZ;     // world-space Z position
    };

    class BrushSampler
    {
    public:
        static std::vector<TileCoord> getAffectedTiles(
            const glm::vec2& brushCenter,
            float radius,
            float worldTileSize);

        static float applyFalloff(float t, BrushFalloff falloff);

        // Iterates over all vertices within the brush radius on the given tile,
        // computing the AABB intersection, distance, and falloff influence.
        // Calls callback(BrushVertexInfo) for each vertex with influence > 0.
        template<typename Func>
        static void forEachInfluencedVertex(
            const TerrainTile& tile,
            const brushes::BrushContext& context,
            Func&& callback);
    };
}

#include "TerrainTile.hpp"

namespace terrain
{
    template<typename Func>
    void BrushSampler::forEachInfluencedVertex(
        const TerrainTile& tile,
        const brushes::BrushContext& context,
        Func&& callback)
    {
        const auto& params = context.params;
        uint32_t vertexCount = tile.config.getVertexCount();
        float vertexSpacing = tile.config.getVertexSpacing();

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

                float influence = applyFalloff(dist, params.falloff);
                size_t idx = static_cast<size_t>(z) * vertexCount + x;

                callback(BrushVertexInfo{idx, influence, worldX, worldZ});
            }
        }
    }
}

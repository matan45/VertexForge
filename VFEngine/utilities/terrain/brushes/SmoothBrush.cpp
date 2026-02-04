#include "SmoothBrush.hpp"
#include "../TerrainTile.hpp"
#include "../BrushSampler.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace terrain::brushes
{
    void SmoothBrush::apply(TerrainTile& tile, const BrushContext& context)
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

        // First pass: compute smoothed target heights
        struct SmoothEntry
        {
            size_t index;
            float targetHeight;
            float influence;
        };

        std::vector<SmoothEntry> entries;
        entries.reserve(static_cast<size_t>(endX - startX + 1) * (endZ - startZ + 1));

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

                // 3x3 neighbor average using world-space sampling for cross-tile access
                float sum = 0.0f;
                float count = 0.0f;

                for (int dz = -1; dz <= 1; ++dz)
                {
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        float sampleX = worldX + static_cast<float>(dx) * vertexSpacing;
                        float sampleZ = worldZ + static_cast<float>(dz) * vertexSpacing;

                        int nx = static_cast<int>(x) + dx;
                        int nz = static_cast<int>(z) + dz;

                        if (nx >= 0 && nx < static_cast<int>(vertexCount) &&
                            nz >= 0 && nz < static_cast<int>(vertexCount))
                        {
                            // Sample from local tile
                            size_t nIdx = static_cast<size_t>(nz) * vertexCount + nx;
                            sum += tile.heightData[nIdx];
                        }
                        else if (context.sampleWorldHeight)
                        {
                            // Sample from neighboring tile
                            sum += context.sampleWorldHeight(sampleX, sampleZ);
                        }
                        else
                        {
                            // Fallback: use current vertex height
                            size_t idx = static_cast<size_t>(z) * vertexCount + x;
                            sum += tile.heightData[idx];
                        }
                        count += 1.0f;
                    }
                }

                float avgHeight = sum / count;
                size_t idx = static_cast<size_t>(z) * vertexCount + x;

                entries.push_back({idx, avgHeight, influence});
            }
        }

        // Second pass: apply smoothing
        for (const auto& entry : entries)
        {
            float current = tile.heightData[entry.index];
            float blendFactor = entry.influence * params.strength * context.deltaTime;
            blendFactor = std::clamp(blendFactor, 0.0f, 1.0f);

            tile.heightData[entry.index] = std::clamp(
                current + (entry.targetHeight - current) * blendFactor,
                tile.config.minHeight,
                tile.config.maxHeight);
        }
    }
}

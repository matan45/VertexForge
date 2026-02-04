#include "SmoothBrush.hpp"
#include "../TerrainTile.hpp"
#include "../BrushSampler.hpp"
#include <algorithm>
#include <vector>

namespace terrain::brushes
{
    void SmoothBrush::apply(TerrainTile& tile, const BrushContext& context)
    {
        const auto& params = context.params;
        uint32_t vertexCount = tile.config.getVertexCount();
        float vertexSpacing = tile.config.getVertexSpacing();

        // First pass: compute smoothed target heights
        struct SmoothEntry
        {
            size_t index;
            float targetHeight;
            float influence;
        };

        std::vector<SmoothEntry> entries;

        BrushSampler::forEachInfluencedVertex(tile, context,
            [&](const BrushVertexInfo& v)
            {
                // Recover local vertex coords from buffer index
                uint32_t x = static_cast<uint32_t>(v.index % vertexCount);
                uint32_t z = static_cast<uint32_t>(v.index / vertexCount);

                // 3x3 neighbor average using world-space sampling for cross-tile access
                float sum = 0.0f;
                float count = 0.0f;

                for (int dz = -1; dz <= 1; ++dz)
                {
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        float sampleX = v.worldX + static_cast<float>(dx) * vertexSpacing;
                        float sampleZ = v.worldZ + static_cast<float>(dz) * vertexSpacing;

                        int nx = static_cast<int>(x) + dx;
                        int nz = static_cast<int>(z) + dz;

                        if (nx >= 0 && nx < static_cast<int>(vertexCount) &&
                            nz >= 0 && nz < static_cast<int>(vertexCount))
                        {
                            size_t nIdx = static_cast<size_t>(nz) * vertexCount + nx;
                            sum += tile.heightData[nIdx];
                        }
                        else if (context.sampleWorldHeight)
                        {
                            sum += context.sampleWorldHeight(sampleX, sampleZ);
                        }
                        else
                        {
                            sum += tile.heightData[v.index];
                        }
                        count += 1.0f;
                    }
                }

                entries.push_back({v.index, sum / count, v.influence});
            });

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

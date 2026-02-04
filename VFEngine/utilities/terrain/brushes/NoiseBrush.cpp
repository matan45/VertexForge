#include "NoiseBrush.hpp"
#include "../TerrainTile.hpp"
#include "../BrushSampler.hpp"
#include <algorithm>
#include <bit>

namespace terrain::brushes
{
    float NoiseBrush::hashNoise(float x, float z)
    {
        // Simple hash-based noise returning [-1, 1]
        uint32_t ix = std::bit_cast<uint32_t>(x);
        uint32_t iz = std::bit_cast<uint32_t>(z);

        uint32_t h = ix ^ (iz * 2654435761u);
        h ^= h >> 16;
        h *= 0x85ebca6bu;
        h ^= h >> 13;
        h *= 0xc2b2ae35u;
        h ^= h >> 16;

        return static_cast<float>(h) / static_cast<float>(0xFFFFFFFFu) * 2.0f - 1.0f;
    }

    void NoiseBrush::apply(TerrainTile& tile, const BrushContext& context)
    {
        float direction = context.invert ? -1.0f : 1.0f;
        const auto& params = context.params;
        constexpr float noiseFrequency = 0.3f;

        BrushSampler::forEachInfluencedVertex(tile, context,
            [&](const BrushVertexInfo& v)
            {
                float noise = hashNoise(v.worldX * noiseFrequency, v.worldZ * noiseFrequency);
                float delta_h = direction * noise * v.influence * params.strength * context.deltaTime;
                tile.heightData[v.index] = std::clamp(
                    tile.heightData[v.index] + delta_h,
                    tile.config.minHeight,
                    tile.config.maxHeight);
            });
    }
}

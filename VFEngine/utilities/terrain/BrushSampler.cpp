#include "BrushSampler.hpp"
#include <cmath>

namespace terrain
{
    std::vector<TileCoord> BrushSampler::getAffectedTiles(
        const glm::vec2& brushCenter,
        float radius,
        float worldTileSize)
    {
        int32_t minTileX = static_cast<int32_t>(std::floor((brushCenter.x - radius) / worldTileSize));
        int32_t minTileZ = static_cast<int32_t>(std::floor((brushCenter.y - radius) / worldTileSize));
        int32_t maxTileX = static_cast<int32_t>(std::floor((brushCenter.x + radius) / worldTileSize));
        int32_t maxTileZ = static_cast<int32_t>(std::floor((brushCenter.y + radius) / worldTileSize));

        std::vector<TileCoord> result;
        result.reserve(static_cast<size_t>((maxTileX - minTileX + 1) * (maxTileZ - minTileZ + 1)));

        for (int32_t z = minTileZ; z <= maxTileZ; ++z)
        {
            for (int32_t x = minTileX; x <= maxTileX; ++x)
            {
                result.emplace_back(x, z);
            }
        }

        return result;
    }

    float BrushSampler::applyFalloff(float t, BrushFalloff falloff)
    {
        if (t >= 1.0f)
        {
            return 0.0f;
        }

        if (t <= 0.0f)
        {
            return 1.0f;
        }

        switch (falloff)
        {
        case BrushFalloff::Constant:
            return 1.0f;
        case BrushFalloff::Linear:
            return 1.0f - t;
        case BrushFalloff::Smooth:
            return 1.0f - t * t * (3.0f - 2.0f * t);
        case BrushFalloff::Sharp:
            return 1.0f - t * t;
        default:
            return 0.0f;
        }
    }
}

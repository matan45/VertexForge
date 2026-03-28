#pragma once

#include "../math/Frustum.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace world
{
    struct HLODCellCoord
    {
        int32_t x = 0;
        int32_t z = 0;
        uint8_t tier = 0;

        HLODCellCoord() = default;
        HLODCellCoord(int32_t x, int32_t z, uint8_t tier) : x(x), z(z), tier(tier) {}

        bool operator==(const HLODCellCoord& other) const
        {
            return x == other.x && z == other.z && tier == other.tier;
        }

        bool operator!=(const HLODCellCoord& other) const
        {
            return !(*this == other);
        }
    };

    struct HLODCellCoordHash
    {
        size_t operator()(const HLODCellCoord& coord) const
        {
            size_t h1 = std::hash<int32_t>{}(coord.x);
            size_t h2 = std::hash<int32_t>{}(coord.z);
            size_t h3 = std::hash<uint8_t>{}(coord.tier);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };

    struct HLODTierConfig
    {
        uint8_t tier = 0;
        uint8_t cellSize = 1;              // sectors per cell edge: 1, 2, 4
        float displayRadius = 10.0f;       // in sector counts, beyond unloadRadius
        float simplificationRatio = 0.1f;  // target triangle ratio vs source
    };

    struct HLODConfig
    {
        bool enabled = false;
        std::vector<HLODTierConfig> tiers;

        static HLODConfig defaultConfig()
        {
            HLODConfig config;
            config.enabled = false;
            config.tiers = {
                {0, 1, 10.0f, 0.10f},
                {1, 2, 20.0f, 0.03f},
                {2, 4, 40.0f, 0.01f}
            };
            return config;
        }
    };

    struct HLODProxySubmesh
    {
        std::string materialPath;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        uint32_t meshletCount = 0;
    };

    struct HLODProxyInfo
    {
        HLODCellCoord cellCoord;
        std::string hlodFilePath;
        math::AABB bounds;
        uint32_t totalVertices = 0;
        uint32_t totalIndices = 0;
        uint32_t submeshCount = 0;
        bool generated = false;
    };

    enum class HLODProxyState : uint8_t
    {
        Unloaded = 0,
        Loading,
        Loaded,
        FadingIn,
        FadingOut,
        Unloading
    };

    using HLODProgressCallback = std::function<void(float progress, std::string_view stage)>;

} // namespace world

#pragma once

#include "TerrainWeightMap.hpp"
#include "TerrainTypes.hpp"
#include <string_view>
#include <unordered_map>

namespace terrain
{
    class TerrainWeightMapAsset
    {
    public:
        static bool save(
            std::string_view path,
            const std::unordered_map<TileCoord, TileWeightMapData, TileCoordHash>& tileWeights,
            uint32_t resolution,
            const std::string& materialPath = "");

        static std::unordered_map<TileCoord, TileWeightMapData, TileCoordHash> load(
            std::string_view path,
            std::string* outMaterialPath = nullptr);
    };
}

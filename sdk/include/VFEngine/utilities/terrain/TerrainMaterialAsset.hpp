#pragma once
#include "TerrainExport.hpp"
#include "TerrainMaterialTypes.hpp"
#include <string_view>
#include <optional>

namespace terrain
{
    class VF_TERRAIN_API TerrainMaterialAsset
    {
    public:
        static std::optional<TerrainMaterialData> load(std::string_view path);
        static bool save(std::string_view path, const TerrainMaterialData& material);
        static TerrainMaterialData createDefault(const std::string& name = "New Terrain Material");
    };
}

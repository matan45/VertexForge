#pragma once

#include "TerrainExport.hpp"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace terrain
{
    struct TerrainFileLocation
    {
        std::string filePath;
        uint64_t baseOffset = 0;
        uint64_t size = 0;
    };

    struct TerrainFileAccess
    {
        std::function<std::optional<TerrainFileLocation>(const std::string&)> locate;
        std::function<std::vector<uint8_t>(const std::string&)> readBytes;
        std::function<bool(const std::string&)> exists;
        std::function<bool()> isArchiveMode;
    };

    VF_TERRAIN_API bool setTerrainFileAccess(TerrainFileAccess access);
    VF_TERRAIN_API void resetTerrainFileAccess();

    VF_TERRAIN_API std::optional<TerrainFileLocation> locateTerrainFile(const std::string& path);
    VF_TERRAIN_API std::vector<uint8_t> readTerrainFileBytes(const std::string& path);
    VF_TERRAIN_API bool terrainFileExists(const std::string& path);
    VF_TERRAIN_API bool terrainArchiveMode();
}

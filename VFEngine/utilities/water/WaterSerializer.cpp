#include "WaterSerializer.hpp"
#include "../print/Log.hpp"
#include "WaterGrid.hpp"
#include "../resource/EndianUtils.hpp"
#include <fstream>
#include <filesystem>

namespace water
{
    namespace fs = std::filesystem;
    using namespace resource::endian;

    bool WaterSerializer::writeHeader(std::ostream& file, const WaterFileHeader& header)
    {
        file.write(WATER_MAGIC.data(), 4);

        writeLE(file, header.versionMajor);
        writeLE(file, header.versionMinor);
        writeLE(file, header.versionPatch);

        writeLE(file, header.tileCount);
        writeLE(file, header.worldTileSize);

        writeLE(file, header.gridMinX);
        writeLE(file, header.gridMinZ);
        writeLE(file, header.gridMaxX);
        writeLE(file, header.gridMaxZ);

        writeLE<uint8_t>(file, header.physicsEnabled ? 1 : 0);

        // Global settings
        const auto& s = header.globalSettings;
        writeLE(file, s.density);
        writeLE(file, s.drag);
        writeLE(file, s.buoyancyStrength);
        writeLE(file, s.waveSpeed);
        writeLE(file, s.waveAmplitude);
        writeLE(file, s.waveFrequency);

        writeLE(file, s.shallowColor.x);
        writeLE(file, s.shallowColor.y);
        writeLE(file, s.shallowColor.z);
        writeLE(file, s.shallowColor.w);

        writeLE(file, s.deepColor.x);
        writeLE(file, s.deepColor.y);
        writeLE(file, s.deepColor.z);
        writeLE(file, s.deepColor.w);

        writeLE(file, s.maxVisibleDepth);
        writeLE(file, s.fresnelPower);
        writeLE(file, s.dudvTiling);
        writeLE(file, s.dudvStrength);
        writeLE(file, s.waveDirectionDegrees);

        return file.good();
    }

    bool WaterSerializer::writeTileData(std::ostream& file, const WaterTile& tile)
    {
        writeLE(file, tile.coord.x);
        writeLE(file, tile.coord.z);
        writeLE(file, tile.waterHeight);
        writeLE(file, tile.waveIntensity);
        writeLE<uint8_t>(file, tile.physicsEnabled ? 1 : 0);

        return file.good();
    }

    bool WaterSerializer::save(
        std::string_view path,
        const WaterGrid& grid,
        const WaterGlobalSettings& globalSettings,
        int32_t gridMinX, int32_t gridMinZ,
        int32_t gridMaxX, int32_t gridMaxZ,
        bool physicsEnabled)
    {
        fs::path filePath(path);
        if (auto parentDir = filePath.parent_path(); !parentDir.empty())
            fs::create_directories(parentDir);

        std::ofstream file(filePath, std::ios::binary);
        if (!file.is_open())
        {
            vfLogError("WaterSerializer: Failed to open file for writing: {}", path);
            return false;
        }

        auto allTiles = grid.getAllTiles();

        WaterFileHeader header;
        header.tileCount = static_cast<uint32_t>(allTiles.size());
        header.worldTileSize = grid.getConfig().worldTileSize;
        header.gridMinX = gridMinX;
        header.gridMinZ = gridMinZ;
        header.gridMaxX = gridMaxX;
        header.gridMaxZ = gridMaxZ;
        header.physicsEnabled = physicsEnabled;
        header.globalSettings = globalSettings;

        if (!writeHeader(file, header))
        {
            vfLogError("WaterSerializer: Failed to write header");
            return false;
        }

        for (const auto* tile : allTiles)
        {
            if (!writeTileData(file, *tile))
            {
                vfLogError("WaterSerializer: Failed to write tile data for ({}, {})",
                           tile->coord.x, tile->coord.z);
                return false;
            }
        }

        vfLogInfo("WaterSerializer: Saved {} tiles to {}", header.tileCount, path);
        return true;
    }
}

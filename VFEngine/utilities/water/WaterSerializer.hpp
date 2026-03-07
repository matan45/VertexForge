#pragma once

#include "WaterTypes.hpp"
#include "WaterTile.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <array>

namespace water
{
    class WaterGrid;

    static constexpr std::array<char, 4> WATER_MAGIC = {'V', 'F', 'W', 'T'};
    static constexpr uint32_t WATER_FORMAT_VERSION_MAJOR = 1;
    static constexpr uint32_t WATER_FORMAT_VERSION_MINOR = 1;
    static constexpr uint32_t WATER_FORMAT_VERSION_PATCH = 0;
    static constexpr uint32_t MAX_REASONABLE_WATER_TILES = 10000;

    struct WaterFileHeader
    {
        uint32_t versionMajor = WATER_FORMAT_VERSION_MAJOR;
        uint32_t versionMinor = WATER_FORMAT_VERSION_MINOR;
        uint32_t versionPatch = WATER_FORMAT_VERSION_PATCH;

        uint32_t tileCount = 0;
        float worldTileSize = 32.0f;

        int32_t gridMinX = 0;
        int32_t gridMinZ = 0;
        int32_t gridMaxX = 0;
        int32_t gridMaxZ = 0;

        bool physicsEnabled = true;

        WaterGlobalSettings globalSettings;
        OceanFFTSettings oceanSettings;
    };

    struct WaterTileData
    {
        int32_t tileX = 0;
        int32_t tileZ = 0;
        float waterHeight = 0.0f;
        float waveIntensity = 1.0f;
        bool physicsEnabled = true;
    };

    struct WaterLoadResult
    {
        WaterFileHeader header;
        std::vector<WaterTileData> tiles;
        bool success = false;
    };

    class WaterSerializer
    {
    public:
        static bool save(
            std::string_view path,
            const WaterGrid& grid,
            const WaterGlobalSettings& globalSettings,
            const OceanFFTSettings& oceanSettings,
            int32_t gridMinX, int32_t gridMinZ,
            int32_t gridMaxX, int32_t gridMaxZ,
            bool physicsEnabled);

        static bool loadAll(
            std::string_view path,
            WaterLoadResult& outResult);

        static bool readHeader(
            std::string_view path,
            WaterFileHeader& outHeader);

    private:
        static bool writeHeader(std::ostream& file, const WaterFileHeader& header);
        static bool writeTileData(std::ostream& file, const WaterTile& tile);
        static bool parseHeader(std::istream& file, WaterFileHeader& outHeader);
        static bool parseTileData(std::istream& file, WaterTileData& outTile);
    };
}

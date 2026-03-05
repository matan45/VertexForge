#pragma once

#include "TerrainTypes.hpp"
#include "TerrainTile.hpp"
#include "TerrainWeightMap.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <array>

namespace terrain
{
    class TerrainGrid;

    static constexpr std::array<char, 4> TERRAIN_MAGIC = {'V', 'F', 'T', 'R'};
    static constexpr uint32_t TERRAIN_FORMAT_VERSION_MAJOR = 1;
    static constexpr uint32_t TERRAIN_FORMAT_VERSION_MINOR = 0;
    static constexpr uint32_t TERRAIN_FORMAT_VERSION_PATCH = 0;
    static constexpr uint32_t MAX_REASONABLE_TERRAIN_TILES = 10000;

    enum class TerrainFormatFlags : uint32_t
    {
        NONE              = 0,
        HAS_WEIGHT_MAPS   = 1 << 0,
        HAS_PHYSICS_DATA  = 1 << 1,
        HAS_MESHLET_CACHE = 1 << 2,
        HAS_HOLE_MASK     = 1 << 3,
    };

    inline TerrainFormatFlags operator|(TerrainFormatFlags a, TerrainFormatFlags b)
    {
        return static_cast<TerrainFormatFlags>(
            static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline bool hasFlag(TerrainFormatFlags flags, TerrainFormatFlags flag)
    {
        return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
    }

    struct TerrainPhysicsConfig
    {
        bool hasCollider = false;
        uint8_t collisionLayer = 0;
        float friction = 0.5f;
        float restitution = 0.0f;
    };

    struct TerrainFileHeader
    {
        uint32_t versionMajor = TERRAIN_FORMAT_VERSION_MAJOR;
        uint32_t versionMinor = TERRAIN_FORMAT_VERSION_MINOR;
        uint32_t versionPatch = TERRAIN_FORMAT_VERSION_PATCH;
        TerrainFormatFlags flags = TerrainFormatFlags::NONE;

        uint32_t tileCount = 0;
        uint8_t resolution = 0;
        float worldTileSize = 32.0f;
        float maxHeight = 100.0f;
        float minHeight = -10.0f;
        float skirtDepth = 5.0f;

        std::array<float, TERRAIN_LOD_COUNT> lodDistances = {100.0f, 300.0f, 600.0f, 1200.0f};

        int32_t gridMinX = 0;
        int32_t gridMinZ = 0;
        int32_t gridMaxX = 0;
        int32_t gridMaxZ = 0;

        std::string materialPath;

        TerrainPhysicsConfig physicsConfig;
    };

    struct TileIndexEntry
    {
        int32_t coordX = 0;
        int32_t coordZ = 0;
        uint64_t heightDataOffset = 0;
        uint32_t heightDataSize = 0;
        uint64_t weightDataOffset = 0;
        uint64_t meshletDataOffset = 0;
        uint64_t holeMaskDataOffset = 0;
    };

    struct TileLoadResult
    {
        TileCoord coord;
        std::vector<float> heightData;
        std::vector<uint8_t> holeMask;
        TileWeightMapData weightMap;
        bool success = false;

        std::array<TileLODData, TERRAIN_LOD_COUNT> lodData;
        bool hasLODCache = false;
    };

    class TerrainSerializer
    {
    public:
        static bool save(
            std::string_view path,
            const TerrainGrid& grid,
            const TerrainTileConfig& config,
            int32_t gridMinX, int32_t gridMinZ,
            int32_t gridMaxX, int32_t gridMaxZ,
            const std::string& materialPath,
            const TerrainPhysicsConfig& physicsConfig = {});

        static bool loadAll(
            std::string_view path,
            TerrainFileHeader& outHeader,
            std::vector<TileLoadResult>& outTiles);

        static bool readHeader(
            std::string_view path,
            TerrainFileHeader& outHeader,
            std::vector<TileIndexEntry>& outIndex);

        static bool readTileHeights(
            std::string_view path,
            const TileIndexEntry& entry,
            std::vector<float>& outHeights);

        static bool readTileWeights(
            std::string_view path,
            const TileIndexEntry& entry,
            TileWeightMapData& outWeights);

        static bool readTileLODData(
            std::string_view path,
            const TileIndexEntry& entry,
            std::array<TileLODData, TERRAIN_LOD_COUNT>& outLODData);

        static bool readTileHoleMask(
            std::string_view path,
            const TileIndexEntry& entry,
            std::vector<uint8_t>& outHoleMask);

    private:
        static bool writeHeader(std::ofstream& file, const TerrainFileHeader& header);
        static bool writeIndexTable(std::ofstream& file, const std::vector<TileIndexEntry>& index);
        static bool writeTileData(
            std::ofstream& file,
            const TerrainTile& tile,
            TerrainFormatFlags flags,
            TileIndexEntry& outEntry);

        static bool writeTileMeshletData(std::ofstream& file, const TerrainTile& tile,
                                         TileIndexEntry& outEntry);

        static bool parseHeader(std::ifstream& file, TerrainFileHeader& outHeader);
        static bool parseIndexTable(std::ifstream& file, uint32_t tileCount,
                                    std::vector<TileIndexEntry>& outIndex);
        static bool parseTileMeshletData(std::ifstream& file, TileLoadResult& result);
    };
}

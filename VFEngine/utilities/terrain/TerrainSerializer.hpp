#pragma once

#include "TerrainTypes.hpp"
#include "TerrainTile.hpp"
#include "TerrainWeightMap.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <unordered_set>
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
        HAS_STREAMING_CONFIG = 1 << 4,
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

    struct TerrainStreamingConfig
    {
        bool enabled = false;
        float loadRadius = 512.0f;
        float unloadRadius = 640.0f;
        int32_t maxLoadsPerFrame = 4;
        int32_t maxUnloadsPerFrame = 4;
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
        TerrainStreamingConfig streamingConfig;
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

    // On-disk serialized size of TileIndexEntry (sum of field sizes, no padding)
    inline constexpr size_t TILE_INDEX_ENTRY_SIZE =
        sizeof(int32_t) + sizeof(int32_t) +     // coordX, coordZ
        sizeof(uint64_t) + sizeof(uint32_t) +    // heightDataOffset, heightDataSize
        sizeof(uint64_t) + sizeof(uint64_t) +    // weightDataOffset, meshletDataOffset
        sizeof(uint64_t);                         // holeMaskDataOffset
    static_assert(TILE_INDEX_ENTRY_SIZE == 44, "TileIndexEntry on-disk size changed — update serialization code");

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
            const TerrainPhysicsConfig& physicsConfig = {},
            const TerrainStreamingConfig& streamingConfig = {});

        static bool loadAll(
            std::string_view path,
            TerrainFileHeader& outHeader,
            std::vector<TileLoadResult>& outTiles);

        static bool readHeader(
            std::string_view path,
            TerrainFileHeader& outHeader,
            std::vector<TileIndexEntry>& outIndex,
            uint64_t* outIndexTableOffset = nullptr);

        static bool saveIncremental(
            std::string_view path,
            const TerrainGrid& grid,
            const std::unordered_set<TileCoord, TileCoordHash>& dirtyCoords,
            const TerrainFileHeader& currentHeader,
            uint64_t indexTableOffset,
            const std::unordered_map<TileCoord, TileIndexEntry, TileCoordHash>& currentIndexMap,
            const TerrainPhysicsConfig& physicsConfig = {},
            const TerrainStreamingConfig& streamingConfig = {});

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
        static bool writeHeader(std::ostream& file, const TerrainFileHeader& header);
        static bool writeIndexTable(std::ostream& file, const std::vector<TileIndexEntry>& index);
        static bool writeTileData(
            std::ostream& file,
            const TerrainTile& tile,
            TerrainFormatFlags flags,
            TileIndexEntry& outEntry);

        static bool writeTileMeshletData(std::ostream& file, const TerrainTile& tile,
                                         TileIndexEntry& outEntry);

        static TerrainFormatFlags computeFlags(const TerrainGrid& grid,
                                                const TerrainPhysicsConfig& physicsConfig,
                                                const TerrainStreamingConfig& streamingConfig);

        static bool parseHeader(std::istream& file, TerrainFileHeader& outHeader);
        static bool parseIndexTable(std::istream& file, uint32_t tileCount,
                                    std::vector<TileIndexEntry>& outIndex);
        static bool parseTileMeshletData(std::istream& file, TileLoadResult& result);
    };
}

#pragma once
#include "TerrainExport.hpp"

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
    static constexpr uint32_t TERRAIN_FORMAT_VERSION_MAJOR = 2;
    static constexpr uint32_t TERRAIN_FORMAT_VERSION_MINOR = 4;
    static constexpr uint32_t TERRAIN_FORMAT_VERSION_PATCH = 0;
    static constexpr uint32_t MAX_REASONABLE_TERRAIN_TILES = 10000;
    inline constexpr uint32_t MAX_TILE_HEIGHT_SAMPLES =
        TILE_VERTEX_COUNTS[static_cast<size_t>(TileResolution::High)] *
        TILE_VERTEX_COUNTS[static_cast<size_t>(TileResolution::High)];
    inline constexpr uint32_t MAX_TILE_HOLE_QUADS =
        TILE_QUAD_COUNTS[static_cast<size_t>(TileResolution::High)] *
        TILE_QUAD_COUNTS[static_cast<size_t>(TileResolution::High)];

    enum class TerrainFormatFlags : uint32_t
    {
        NONE              = 0,
        HAS_WEIGHT_MAPS   = 1 << 0,
        HAS_PHYSICS_DATA  = 1 << 1,
        HAS_MESHLET_CACHE = 1 << 2,
        HAS_HOLE_MASK     = 1 << 3,
        HAS_STREAMING_CONFIG = 1 << 4,
        HAS_COMPRESSED_DATA  = 1 << 5,
        HAS_CAVE_DATA        = 1 << 7,
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

#pragma warning(push)
#pragma warning(disable: 4251)
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

        std::array<float, TERRAIN_LOD_COUNT> lodDistances = {100.0f, 300.0f, 600.0f, 1200.0f, 2000.0f, 3500.0f};

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
        uint64_t caveSdfDataOffset = 0;
    };

    // On-disk serialized size of TileIndexEntry (sum of field sizes, no padding)
    inline constexpr size_t TILE_INDEX_ENTRY_SIZE =
        sizeof(int32_t) + sizeof(int32_t) +     // coordX, coordZ
        sizeof(uint64_t) + sizeof(uint32_t) +    // heightDataOffset, heightDataSize
        sizeof(uint64_t) + sizeof(uint64_t) +    // weightDataOffset, meshletDataOffset
        sizeof(uint64_t) +                        // holeMaskDataOffset
        sizeof(uint64_t);                         // caveSdfDataOffset
    static_assert(TILE_INDEX_ENTRY_SIZE == 52, "TileIndexEntry on-disk size changed — update serialization code");

    struct TileLoadResult
    {
        TileCoord coord;
        std::vector<float> heightData;
        std::vector<uint8_t> holeMask;
        TileWeightMapData weightMap;
        bool success = false;

        std::array<TileLODData, TERRAIN_LOD_COUNT> lodData;
        bool hasLODCache = false;

        std::unique_ptr<CaveSDFData> caveData;
        bool hasCaveData = false;
    };

    struct TerrainSaveParams
    {
        std::string_view path;
        const TerrainGrid* grid = nullptr;
        TerrainTileConfig config;
        int32_t gridMinX = 0;
        int32_t gridMinZ = 0;
        int32_t gridMaxX = 0;
        int32_t gridMaxZ = 0;
        std::string materialPath;
        TerrainPhysicsConfig physicsConfig;
        TerrainStreamingConfig streamingConfig;
    };

    struct TerrainIncrementalSaveParams
    {
        std::string_view path;
        const TerrainGrid* grid = nullptr;
        const std::unordered_set<TileCoord, TileCoordHash>* dirtyCoords = nullptr;
        TerrainFileHeader currentHeader;
        uint64_t indexTableOffset = 0;
        const std::unordered_map<TileCoord, TileIndexEntry, TileCoordHash>* currentIndexMap = nullptr;
        TerrainPhysicsConfig physicsConfig;
        TerrainStreamingConfig streamingConfig;
    };

    class VF_TERRAIN_API TerrainSerializer
    {
    public:
        static bool save(const TerrainSaveParams& params);

        static bool readHeader(
            std::string_view path,
            TerrainFileHeader& outHeader,
            std::vector<TileIndexEntry>& outIndex,
            uint64_t* outIndexTableOffset = nullptr);

        static bool saveIncremental(const TerrainIncrementalSaveParams& params);

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

        static bool readTileCaveData(
            std::string_view path,
            const TileIndexEntry& entry,
            CaveSDFData& outCaveData);

        static bool writeTileCaveData(
            std::ostream& file,
            const TerrainTile& tile,
            TileIndexEntry& outEntry);

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

        static TerrainFileHeader buildSaveHeader(const TerrainSaveParams& params,
                                                  TerrainFormatFlags flags,
                                                  uint32_t tileCount);

        static bool writeAllTileData(std::ostream& file,
                                      const std::vector<const TerrainTile*>& tiles,
                                      TerrainFormatFlags flags,
                                      std::vector<TileIndexEntry>& indexEntries);

        static bool writeFullSaveToStream(std::ostream& file,
                                           const TerrainFileHeader& header,
                                           const std::vector<const TerrainTile*>& tiles,
                                           TerrainFormatFlags flags);

        static bool validateIncrementalFlags(TerrainFormatFlags currentFlags,
                                              TerrainFormatFlags newFlags);

        static std::vector<TileIndexEntry> buildSortedIndex(
            const std::unordered_map<TileCoord, TileIndexEntry, TileCoordHash>& indexMap);

        static bool writeIncrementalTiles(std::ostream& file,
                                           const TerrainGrid& grid,
                                           const std::unordered_set<TileCoord, TileCoordHash>& dirtyCoords,
                                           TerrainFormatFlags flags,
                                           std::vector<TileIndexEntry>& indexEntries);

        static bool writeIncrementalHeaderAndIndex(std::ostream& file,
                                                    const TerrainFileHeader& updatedHeader,
                                                    uint64_t indexTableOffset,
                                                    const std::vector<TileIndexEntry>& indexEntries);

        static bool parseHeader(std::istream& file, TerrainFileHeader& outHeader);
        static bool parseIndexTable(std::istream& file, uint32_t tileCount,
                                    std::vector<TileIndexEntry>& outIndex);
        static bool parseTileMeshletData(std::istream& file, TileLoadResult& result);
    };
#pragma warning(pop)
}

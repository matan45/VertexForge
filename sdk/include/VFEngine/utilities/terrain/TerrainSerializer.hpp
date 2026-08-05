#pragma once
#include "TerrainExport.hpp"

#include "TerrainTypes.hpp"
#include "TerrainTile.hpp"
#include "TerrainWeightMap.hpp"
#include <iosfwd>
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
    class TerrainHeightLayerStore;

    static constexpr std::array<char, 4> TERRAIN_MAGIC = {'V', 'F', 'T', 'R'};
    static constexpr uint32_t TERRAIN_FORMAT_VERSION_MAJOR = 2;
    // 2.5.0 added TileIndexEntry::payloadSize, which is what lets obsolete bytes be derived and
    // lets compaction relocate a tile record without decoding it. The reader rejects any other
    // triple outright — there is no upgrade path, a 2.4.0 file must be re-saved.
    static constexpr uint32_t TERRAIN_FORMAT_VERSION_MINOR = 5;
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
        // VK-1646. A `<path>.vfterrainlayers` sidecar carries this terrain's authoritative base
        // heights and reserved layer stack. The bit changes nothing about how the tiles below are
        // read — VFTR still holds the flattened composite — it only tells a loader that layer
        // AUTHORING depends on a second file, so a missing or mismatched one is worth a diagnostic
        // instead of being invisible. Deliberately does not alter the header layout, so a file
        // with it set stays byte-compatible with VFTR 2.5.0.
        HAS_EDIT_LAYER_SIDECAR = 1 << 6,
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

    // Byte offset of the uint32_t materialPath length prefix, i.e. the size of the
    // fixed-width part of the header. Derived from the field sizes writeHeader() emits so it
    // cannot silently rot the way a hardcoded literal would (TERRAIN_LOD_COUNT has grown before).
    inline constexpr uint64_t TERRAIN_HEADER_PATH_LENGTH_OFFSET =
        4 +                                     // magic 'VFTR'
        3 * sizeof(uint32_t) +                  // versionMajor / versionMinor / versionPatch
        sizeof(uint32_t) +                      // flags
        sizeof(uint32_t) +                      // tileCount
        sizeof(uint8_t) +                       // resolution
        4 * sizeof(float) +                     // worldTileSize, maxHeight, minHeight, skirtDepth
        TERRAIN_LOD_COUNT * sizeof(float) +     // lodDistances
        4 * sizeof(int32_t);                    // gridMinX, gridMinZ, gridMaxX, gridMaxZ
    static_assert(TERRAIN_HEADER_PATH_LENGTH_OFFSET == 81,
                  "VFTR header prefix changed — update every reader that seeks past it");

    // Optional trailing header blocks, written only when the matching flag is set.
    inline constexpr uint64_t TERRAIN_HEADER_PHYSICS_BLOCK_SIZE =
        sizeof(uint8_t) + sizeof(uint8_t) + sizeof(float) + sizeof(float);
    inline constexpr uint64_t TERRAIN_HEADER_STREAMING_BLOCK_SIZE =
        sizeof(uint8_t) + sizeof(float) + sizeof(float) + sizeof(int32_t) + sizeof(int32_t);
    // VK-1646. Present only when HAS_EDIT_LAYER_SIDECAR is set: the opaque id that binds this
    // terrain to its `.vfterrainlayers` sidecar. Deliberately in the flag-gated tail rather than
    // the fixed prefix, so a terrain without layers is byte-identical to one this build's
    // predecessor wrote and no version bump is needed.
    inline constexpr uint64_t TERRAIN_HEADER_EDIT_LAYER_BLOCK_SIZE = sizeof(uint64_t);
    static_assert(TERRAIN_HEADER_PHYSICS_BLOCK_SIZE == 10 && TERRAIN_HEADER_STREAMING_BLOCK_SIZE == 17 &&
                  TERRAIN_HEADER_EDIT_LAYER_BLOCK_SIZE == 8,
                  "VFTR optional header blocks changed — update every reader that seeks past them");

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

        // VK-1646. An opaque 64-bit id, regenerated every time this terrain's `.vfterrainlayers`
        // sidecar is (re)written and stamped into both files. Meaningful only when
        // HAS_EDIT_LAYER_SIDECAR is set.
        //
        // A NONCE, not a content hash. Two existing paths rewrite a terrain's bytes without
        // changing a single height — compaction relocates every record
        // (TerrainSerializerCompact.cpp) and a terrain-material rename splices a new path into the
        // header and shifts every offset (AssetReferenceScanner::updateTerrainFile). A hash over
        // file bytes would call both of those "stale" and cost the artist their entire layer stack
        // for renaming a material. Both copy the header through verbatim, so an id survives them
        // for free — which is the whole reason it lives here rather than being derived.
        //
        // Random rather than a counter: a counter collides after a restore from backup, where two
        // different generations legitimately share a number.
        uint64_t editLayerGenerationId = 0;
    };

    // Serialized size of the header, which is also the index table offset: the format does not
    // store that offset, readHeader() recovers it as the stream position after parseHeader().
    // Because tile offsets in the index are absolute, an in-place header rewrite is safe only
    // while this value is unchanged — see TerrainSerializer::saveIncremental().
    inline uint64_t serializedHeaderSize(const TerrainFileHeader& header)
    {
        return TERRAIN_HEADER_PATH_LENGTH_OFFSET +
               sizeof(uint32_t) +                                // materialPath length prefix
               header.materialPath.size() +
               (hasFlag(header.flags, TerrainFormatFlags::HAS_PHYSICS_DATA)
                    ? TERRAIN_HEADER_PHYSICS_BLOCK_SIZE : 0) +
               (hasFlag(header.flags, TerrainFormatFlags::HAS_STREAMING_CONFIG)
                    ? TERRAIN_HEADER_STREAMING_BLOCK_SIZE : 0) +
               (hasFlag(header.flags, TerrainFormatFlags::HAS_EDIT_LAYER_SIDECAR)
                    ? TERRAIN_HEADER_EDIT_LAYER_BLOCK_SIZE : 0);
    }

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
        // Total bytes of the tile's record, which writeTileData() emits as one contiguous run
        // starting at heightDataOffset. Without it a tile's extent is unknowable from the index —
        // only heightDataSize is stored, the other four sections have no size — so obsolete bytes
        // could not be derived and compaction could not relocate a record without decoding it.
        // Appended last on purpose: every earlier field keeps its byte position inside the entry.
        uint32_t payloadSize = 0;
    };

    // Byte position of each field inside a serialized TileIndexEntry. AssetReferenceScanner patches
    // offsets directly in the file bytes, so these must not be hand-copied — a stale copy of this
    // arithmetic silently corrupted every terrain it rewrote once already.
    inline constexpr size_t TILE_INDEX_COORD_X_FIELD_OFFSET = 0;
    inline constexpr size_t TILE_INDEX_COORD_Z_FIELD_OFFSET =
        TILE_INDEX_COORD_X_FIELD_OFFSET + sizeof(int32_t);
    inline constexpr size_t TILE_INDEX_HEIGHT_OFFSET_FIELD_OFFSET =
        TILE_INDEX_COORD_Z_FIELD_OFFSET + sizeof(int32_t);
    inline constexpr size_t TILE_INDEX_HEIGHT_SIZE_FIELD_OFFSET =
        TILE_INDEX_HEIGHT_OFFSET_FIELD_OFFSET + sizeof(uint64_t);
    inline constexpr size_t TILE_INDEX_WEIGHT_OFFSET_FIELD_OFFSET =
        TILE_INDEX_HEIGHT_SIZE_FIELD_OFFSET + sizeof(uint32_t);
    inline constexpr size_t TILE_INDEX_MESHLET_OFFSET_FIELD_OFFSET =
        TILE_INDEX_WEIGHT_OFFSET_FIELD_OFFSET + sizeof(uint64_t);
    inline constexpr size_t TILE_INDEX_HOLE_MASK_OFFSET_FIELD_OFFSET =
        TILE_INDEX_MESHLET_OFFSET_FIELD_OFFSET + sizeof(uint64_t);
    inline constexpr size_t TILE_INDEX_CAVE_SDF_OFFSET_FIELD_OFFSET =
        TILE_INDEX_HOLE_MASK_OFFSET_FIELD_OFFSET + sizeof(uint64_t);
    inline constexpr size_t TILE_INDEX_PAYLOAD_SIZE_FIELD_OFFSET =
        TILE_INDEX_CAVE_SDF_OFFSET_FIELD_OFFSET + sizeof(uint64_t);

    // On-disk serialized size of TileIndexEntry (sum of field sizes, no padding)
    inline constexpr size_t TILE_INDEX_ENTRY_SIZE =
        TILE_INDEX_PAYLOAD_SIZE_FIELD_OFFSET + sizeof(uint32_t);
    static_assert(TILE_INDEX_ENTRY_SIZE == 56, "TileIndexEntry on-disk size changed — update serialization code");
    static_assert(TILE_INDEX_HEIGHT_OFFSET_FIELD_OFFSET == 8 &&
                  TILE_INDEX_WEIGHT_OFFSET_FIELD_OFFSET == 20 &&
                  TILE_INDEX_MESHLET_OFFSET_FIELD_OFFSET == 28 &&
                  TILE_INDEX_HOLE_MASK_OFFSET_FIELD_OFFSET == 36 &&
                  TILE_INDEX_CAVE_SDF_OFFSET_FIELD_OFFSET == 44,
                  "TileIndexEntry field order changed — update every reader that patches entries in place");

    // Byte accounting for one .vfTerrain. Derived from what the index references rather than tracked
    // in a persisted counter, so it is exact again after any interruption: a payload appended by a
    // save that never committed is simply not referenced, and therefore counts as obsolete.
    struct TerrainFileOccupancy
    {
        uint64_t fileSize = 0;
        uint64_t overheadBytes = 0;   // header + index table
        uint64_t liveBytes = 0;       // Σ payloadSize over the indexed tiles
        uint64_t obsoleteBytes = 0;   // whatever is left over
        bool consistent = true;       // false when the referenced bytes exceed the file
    };

    // A tile record cannot legitimately approach 4 GiB. Bounding it stops a corrupt index from
    // driving compaction into an enormous copy.
    inline constexpr uint64_t MAX_TILE_PAYLOAD_BYTES = 256ull * 1024ull * 1024ull;

    // Compaction policy. The floor stops a small terrain rewriting itself on every save (25% of a
    // 400 KB file is a single Low-resolution tile); above it, either a quarter of the live bytes or
    // a flat 64 MiB is enough — a ratio alone would let a 1 GB file carry 250 MB of dead weight.
    inline constexpr uint64_t TERRAIN_COMPACT_MIN_OBSOLETE_BYTES = 1ull * 1024ull * 1024ull;
    inline constexpr uint64_t TERRAIN_COMPACT_ABSOLUTE_BYTES = 64ull * 1024ull * 1024ull;
    inline constexpr uint64_t TERRAIN_COMPACT_RATIO_DIVISOR = 4;

    inline TerrainFileOccupancy terrainFileOccupancy(const TerrainFileHeader& header,
                                                     const std::vector<TileIndexEntry>& index,
                                                     uint64_t indexTableOffset,
                                                     uint64_t fileSize)
    {
        TerrainFileOccupancy usage;
        usage.fileSize = fileSize;
        usage.overheadBytes = indexTableOffset +
                              static_cast<uint64_t>(header.tileCount) * TILE_INDEX_ENTRY_SIZE;
        for (const auto& entry : index)
            usage.liveBytes += entry.payloadSize;

        const uint64_t referenced = usage.overheadBytes + usage.liveBytes;
        // Unsigned arithmetic would wrap a truncated file into an enormous "garbage" figure and
        // trigger a compaction of a file that is already broken.
        usage.consistent = referenced <= fileSize;
        usage.obsoleteBytes = usage.consistent ? fileSize - referenced : 0;
        return usage;
    }

    inline bool shouldCompactTerrainFile(const TerrainFileOccupancy& usage)
    {
        if (!usage.consistent || usage.obsoleteBytes < TERRAIN_COMPACT_MIN_OBSOLETE_BYTES)
            return false;
        return usage.obsoleteBytes >= TERRAIN_COMPACT_ABSOLUTE_BYTES ||
               usage.obsoleteBytes * TERRAIN_COMPACT_RATIO_DIVISOR >= usage.liveBytes;
    }

    // Why saveIncremental cannot just return bool: TerrainService::saveTerrainIncremental() runs on
    // a background thread and answers failure by calling saveTerrain(), which writes only the tiles
    // resident in the grid. With streaming on that silently deletes every unloaded tile. So a real
    // IO error must be distinguishable from "this edit needs a full save" — only the latter is safe
    // to fall back on, and only because prepareSaveIncremental() forced residency for those cases.
    enum class TerrainIncrementalSaveResult
    {
        Success,
        NeedsFullSave,
        Failed
    };

    enum class TerrainRecoveryResult
    {
        NotNeeded,
        Discarded,
        Redone,
        Failed
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

        // VK-1646. When this is non-null and non-empty the save becomes a two-file commit: it also
        // writes `<path>.vfterrainlayers` and sets HAS_EDIT_LAYER_SIDECAR. Null (or empty) keeps
        // the pre-VK-1646 behaviour exactly, including deleting a sidecar left over from a stack
        // the artist has since emptied.
        //
        // Passed rather than read off params.grid so a caller can save a terrain without also
        // committing to persisting its authoring state — the compaction and test paths both do.
        const TerrainHeightLayerStore* heightLayers = nullptr;

        // asset::AssetGUID::getValue() of the owning .vfterrain, stamped into the sidecar so a
        // copied or renamed asset can be told apart from the one the sidecar was written for.
        // Zero when the caller has no GUID yet; the sidecar records it as "unknown" and the load
        // path treats a zero on either side as "do not compare".
        uint64_t terrainGuid = 0;
    };

    struct TerrainIncrementalSaveParams
    {
        std::string_view path;
        const TerrainGrid* grid = nullptr;
        const std::unordered_set<TileCoord, TileCoordHash>* dirtyCoords = nullptr;
        // The header as it currently sits on disk. indexTableOffset must match its
        // serializedHeaderSize() — saveIncremental() verifies this before touching the file.
        TerrainFileHeader currentHeader;
        uint64_t indexTableOffset = 0;
        const std::unordered_map<TileCoord, TileIndexEntry, TileCoordHash>* currentIndexMap = nullptr;
        // The material path to persist. Only writable in place while its length matches the one
        // in currentHeader; any other length moves the index table and forces a full save.
        std::string materialPath;
        TerrainPhysicsConfig physicsConfig;
        TerrainStreamingConfig streamingConfig;

        // VK-1646, same contract as TerrainSaveParams. Adding or removing the sidecar resizes the
        // header, so either transition is refused as NeedsFullSave; keeping one is free, because
        // only the id's VALUE changes and the block is already there.
        const TerrainHeightLayerStore* heightLayers = nullptr;
        uint64_t terrainGuid = 0;
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

        static TerrainIncrementalSaveResult saveIncremental(const TerrainIncrementalSaveParams& params);

        // Reclaims the bytes saveIncremental() orphaned, by copying each live tile record verbatim
        // into a fresh file and shifting its index offsets. It decodes nothing and needs no
        // TerrainGrid, so — unlike a full save — it cannot drop a streamed-out tile.
        static bool compact(std::string_view path);
        static bool compactIfNeeded(std::string_view path, bool* outCompacted = nullptr);

        // Applies or discards an interrupted incremental commit and sweeps a stale .tmp.
        // Idempotent, and a no-op in archive mode or for a terrain packed inside a .vfpak.
        // Must run before anything reads the file for real work — see the note on TerrainFileCache.
        static TerrainRecoveryResult recoverPending(std::string_view path);

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

        static bool validateIncrementalHeaderLayout(const TerrainFileHeader& updatedHeader,
                                                     uint64_t indexTableOffset);

        // computeFlags() only sees the tiles resident in the grid, so on the incremental path it
        // would clear any flag that only a streamed-out tile justifies — and writeTileData() then
        // drops that section from the tiles it rewrites. Union with what is already on disk; only
        // the physics and streaming bits are authoritatively config-derived, and toggling either
        // resizes the header, which validateIncrementalHeaderLayout() already refuses.
        static TerrainFormatFlags mergeIncrementalFlags(TerrainFormatFlags onDisk,
                                                         TerrainFormatFlags computed);

        static bool serializeHeaderToBytes(const TerrainFileHeader& header,
                                            std::vector<uint8_t>& out);
        static bool serializeIndexToBytes(const std::vector<TileIndexEntry>& index,
                                           std::vector<uint8_t>& out);
        static bool commitIncrementalBuffers(std::fstream& file,
                                              uint64_t indexTableOffset,
                                              const std::vector<uint8_t>& headerBytes,
                                              const std::vector<uint8_t>& indexBytes);

        static bool validateRecordExtents(const TerrainFileHeader& header,
                                           const std::vector<TileIndexEntry>& index,
                                           uint64_t indexTableOffset,
                                           uint64_t fileSize);

        // Lock-free internals: the public entry points already hold terrainFileMutex(), which is
        // not recursive, so these must never be called from outside one.
        static bool readHeaderLocked(std::string_view path,
                                      TerrainFileHeader& outHeader,
                                      std::vector<TileIndexEntry>& outIndex,
                                      uint64_t* outIndexTableOffset);
        static TerrainRecoveryResult recoverPendingLocked(std::string_view path);
        static bool compactLocked(std::string_view path);

        static std::vector<TileIndexEntry> buildSortedIndex(
            const std::unordered_map<TileCoord, TileIndexEntry, TileCoordHash>& indexMap);

        static bool writeIncrementalTiles(std::ostream& file,
                                           const TerrainGrid& grid,
                                           const std::unordered_set<TileCoord, TileCoordHash>& dirtyCoords,
                                           TerrainFormatFlags flags,
                                           std::vector<TileIndexEntry>& indexEntries);

        static bool parseHeader(std::istream& file, TerrainFileHeader& outHeader);
        static bool parseIndexTable(std::istream& file, uint32_t tileCount,
                                    std::vector<TileIndexEntry>& outIndex);
        static bool parseTileMeshletData(std::istream& file, TileLoadResult& result);
    };
#pragma warning(pop)
}

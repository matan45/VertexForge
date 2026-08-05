#pragma once

#include "TerrainExport.hpp"

#include "TerrainHeightLayerStore.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace terrain
{
    // VK-1646. The `.vfterrainlayers` sidecar ("VFTL"): the authoring half of a terrain.
    //
    // VFTR stores the DERIVED, flattened composite that geometry, physics, streaming and the
    // shipped runtime all consume. It cannot also carry the authoritative bases and the layer
    // stack: its index entries are a fixed 56 bytes, and its index offset is not stored at all but
    // recovered as the size of a header that a variable-length materialPath already makes movable
    // (TerrainSerializer.hpp). Widening either would rewrite a format three subsystems embed by
    // value. So the authoring data lives beside it, in a file with its own version.
    //
    // The two are bound by VFTR flag bit 6 plus an opaque generation id stamped into both files by
    // one commit. That pair is what makes every failure mode diagnosable rather than silent: see
    // readTerrainLayerSidecar's status values.
    inline constexpr std::array<char, 4> TERRAIN_LAYER_MAGIC = {'V', 'F', 'T', 'L'};

    // Independent of VFTR's version on purpose — the two formats change for different reasons.
    inline constexpr uint32_t TERRAIN_LAYER_VERSION_MAJOR = 1;
    inline constexpr uint32_t TERRAIN_LAYER_VERSION_MINOR = 0;
    inline constexpr uint32_t TERRAIN_LAYER_VERSION_PATCH = 0;

    inline constexpr std::string_view TERRAIN_LAYER_EXTENSION = ".vfterrainlayers";

    // Closes the file the way VK-1644's journal does: a magic word that only a writer which
    // reached the end emits, so a torn write is a fact rather than an inference.
    inline constexpr uint64_t TERRAIN_LAYER_COMMIT_MAGIC = 0x5646544C434D5449ull;

    // Byte offsets of the two fields rebindTerrainLayerSidecar() patches. Derived from the field
    // sizes the writer emits rather than hardcoded, for the same reason
    // TERRAIN_HEADER_PATH_LENGTH_OFFSET is.
    inline constexpr uint64_t TERRAIN_LAYER_GUID_OFFSET =
        4 +                     // magic 'VFTL'
        3 * sizeof(uint32_t);   // versionMajor / versionMinor / versionPatch
    inline constexpr uint64_t TERRAIN_LAYER_GENERATION_ID_OFFSET =
        TERRAIN_LAYER_GUID_OFFSET + sizeof(uint64_t);

    inline constexpr uint64_t TERRAIN_LAYER_HEADER_SIZE =
        TERRAIN_LAYER_GENERATION_ID_OFFSET +
        sizeof(uint64_t) +          // generationId
        4 * sizeof(int32_t) +       // gridMinX / gridMinZ / gridMaxX / gridMaxZ
        sizeof(uint8_t) +           // resolution
        sizeof(float) +             // worldTileSize
        2 * sizeof(uint32_t) +      // layerCount / baseBlockCount
        2 * sizeof(uint64_t);       // layerTableOffset / baseIndexOffset
    static_assert(TERRAIN_LAYER_GUID_OFFSET == 16 && TERRAIN_LAYER_HEADER_SIZE == 77,
                  "VFTL header layout changed — update rebindTerrainLayerSidecar and the tests");

    // coordX, coordZ, blockOffset, byteLength, vertexCount, blockCrc32.
    inline constexpr uint64_t TERRAIN_LAYER_BASE_INDEX_ENTRY_SIZE =
        2 * sizeof(int32_t) + sizeof(uint64_t) + 3 * sizeof(uint32_t);
    static_assert(TERRAIN_LAYER_BASE_INDEX_ENTRY_SIZE == 28,
                  "VFTL base index entry size changed — update every reader");

    inline constexpr uint64_t TERRAIN_LAYER_TRAILER_SIZE = sizeof(uint32_t) + sizeof(uint64_t);

    // A layer's polyline is authored, not generated, so a few thousand samples is already absurd.
    // Bounding it stops a corrupt count from driving a multi-gigabyte allocation before the CRC
    // that would have rejected it is ever reached.
    inline constexpr uint32_t MAX_LAYER_SPLINE_SAMPLES = 1u << 20;

    // NOT `MAX_TERRAIN_LAYERS` — that name is taken, by the material palette's cap of 32
    // (`TerrainMaterialTypes.hpp`). The two "layers" are unrelated: that one bounds how many
    // material entries a tile can blend, this one how many edit-layer records a sidecar may carry.
    inline constexpr uint32_t MAX_TERRAIN_EDIT_LAYERS = 4096;
    // Mirrors TerrainSerializer.hpp's MAX_REASONABLE_TERRAIN_TILES rather than including it: the
    // serializer includes THIS header, so the dependency has to run one way only.
    inline constexpr uint32_t MAX_LAYER_AFFECTED_TILES = 10000;

    // Identity of the terrain this sidecar belongs to, and of the exact VFTR generation it was
    // written beside. Everything here is metadata; the payload lives in the store.
    struct TerrainLayerSidecarMeta
    {
        // The owning .vfterrain's AssetGUID value (asset::AssetGUID::getValue()). Kept as a plain
        // uint64_t so Terrain.dll does not have to know the asset module exists.
        uint64_t terrainGuid = 0;

        // Must equal TerrainFileHeader::editLayerGenerationId in the terrain beside it. Both are
        // stamped with one freshly generated value in the same commit, so the pair is consistent by
        // construction and every later divergence — a restored backup on either side, a commit
        // interrupted between the two renames, a save by a build that cleared bit 6 — shows up as
        // a mismatch.
        //
        // Not a hash of the terrain's bytes: compaction and a terrain-material rename both rewrite
        // those bytes wholesale without changing one height, and a hash would call the sidecar
        // stale for either.
        uint64_t generationId = 0;

        int32_t gridMinX = 0;
        int32_t gridMinZ = 0;
        int32_t gridMaxX = 0;
        int32_t gridMaxZ = 0;
        uint8_t resolution = 0;
        float worldTileSize = 32.0f;
    };

    enum class TerrainLayerSidecarStatus
    {
        Ok,        // parsed, checksums clean, generation matches — the store was populated
        Absent,    // no file beside the terrain
        Invalid,   // wrong magic/version, truncated, or a checksum failed
        Stale,     // structurally sound, but written against a different VFTR generation
        Degraded,  // structurally sound, but carries a layer type this build cannot evaluate
    };

    // `<terrainPath>.vfterrainlayers`. Appended, never substituted — the same rule .vfmeta,
    // .vfCollider and .vftrj follow, which is what lets one "does this sidecar belong to that
    // asset" answer serve all of them.
    [[nodiscard]] VF_TERRAIN_API std::filesystem::path terrainLayerSidecarPath(
        const std::filesystem::path& terrainPath);

    // A fresh, non-zero, random 64-bit id. Stamped into a terrain's header and into the sidecar
    // written beside it in the same commit; the two matching is what makes the pair valid.
    //
    // Random rather than sequential: a counter collides after a restore from backup, where two
    // different generations legitimately hold the same number.
    [[nodiscard]] VF_TERRAIN_API uint64_t newLayerGenerationId();

    // Writes the store to `path` in full. There is no incremental sidecar: the base blocks are
    // only meaningful as a set consistent with one VFTR generation, and a partial rewrite could
    // not honour that.
    //
    // Every layer must carry a known HeightLayerType. The writer refuses rather than dropping a
    // layer the artist can still see on screen.
    [[nodiscard]] VF_TERRAIN_API bool writeTerrainLayerSidecar(
        const std::filesystem::path& path,
        const TerrainLayerSidecarMeta& meta,
        const TerrainHeightLayerStore& store);

    // Reads `path` and, ONLY on Ok, populates `outStore` — every other status leaves it untouched.
    // That is deliberate: a half-loaded stack would still make its tiles covered (coverage is
    // sticky), and the next sculpt would then route into a base whose layers are incomplete.
    //
    // `outMeta` is filled whenever the header itself parsed, so a caller can name the two
    // generations in a Stale diagnostic.
    [[nodiscard]] VF_TERRAIN_API TerrainLayerSidecarStatus readTerrainLayerSidecar(
        const std::filesystem::path& path,
        uint64_t expectedGenerationId,
        TerrainLayerSidecarMeta& outMeta,
        TerrainHeightLayerStore& outStore);

    // Reads the header alone. For the orphan check and for diagnostics, where decoding tens of
    // megabytes of base blocks to answer "whose is this?" would be absurd.
    [[nodiscard]] VF_TERRAIN_API TerrainLayerSidecarStatus peekTerrainLayerSidecar(
        const std::filesystem::path& path,
        TerrainLayerSidecarMeta& outMeta);

    // Repoints an existing sidecar at a different terrain identity, without decoding or re-emitting
    // a single base block.
    //
    // For duplicating a terrain asset: the copy is given a fresh .vfmeta GUID, so a byte-identical
    // sidecar would read as an orphan beside it. The generation id is passed through unchanged —
    // fs::copy_file reproduced the terrain byte for byte, so the copy still carries the same id in
    // its header.
    [[nodiscard]] VF_TERRAIN_API bool rebindTerrainLayerSidecar(
        const std::filesystem::path& path,
        uint64_t newTerrainGuid,
        uint64_t newGenerationId);
}

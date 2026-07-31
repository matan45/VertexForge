#include <doctest.h>

#include "test_terrain_serializer_fixture.hpp"

#include <resource/EndianUtils.hpp>
#include <resource/MeshletTypes.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

namespace
{
    constexpr uint32_t TEST_MAX_VERTICES_PER_LOD = 1u << 20;
    constexpr uint32_t TEST_MAX_INDICES_PER_LOD = TEST_MAX_VERTICES_PER_LOD * 6u;
    constexpr uint32_t TEST_MAX_MESHLETS_PER_LOD = TEST_MAX_VERTICES_PER_LOD;
    constexpr uint64_t SERIALIZED_VERTEX_SIZE = 16u * sizeof(uint32_t);
    constexpr uint64_t MESHLET_HEADER_SIZE =
        static_cast<uint64_t>(terrain::TERRAIN_LOD_COUNT) * 3u * sizeof(uint32_t);

    static_assert(terrain::MAX_TILE_HEIGHT_SAMPLES == 129u * 129u);
    static_assert(terrain::MAX_TILE_HOLE_QUADS == 128u * 128u);

    bool saveValidationTerrain(
        const ScopedTerrainTestFile& file,
        terrain::TileResolution resolution)
    {
        const auto config = makeTerrainTestConfig(resolution);
        auto grid = makePopulatedTerrainTestGrid(config, {{0, 0}});
        auto params = makeTerrainTestSaveParams(file.string(), *grid, config);
        return terrain::TerrainSerializer::save(params);
    }

    template<typename T>
    bool writeValueAt(const std::filesystem::path& path, uint64_t offset, T value)
    {
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        if (!file.is_open())
            return false;
        file.seekp(static_cast<std::streamoff>(offset));
        resource::endian::writeLE<T>(file, value);
        file.flush();
        return file.good();
    }

    template<typename T>
    bool readValueAt(const std::filesystem::path& path, uint64_t offset, T& value)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
            return false;
        file.seekg(static_cast<std::streamoff>(offset));
        value = resource::endian::readLE<T>(file);
        return file.good();
    }

    bool headerReads(const ScopedTerrainTestFile& file)
    {
        terrain::TerrainFileHeader header;
        std::vector<terrain::TileIndexEntry> index;
        return terrain::TerrainSerializer::readHeader(file.string(), header, index);
    }

    uint64_t firstLODIndexCountOffset(
        const std::filesystem::path& path,
        uint64_t meshletDataOffset)
    {
        uint64_t cursor = meshletDataOffset + MESHLET_HEADER_SIZE;
        for (uint32_t lod = 0; lod < terrain::TERRAIN_LOD_COUNT; ++lod)
        {
            uint32_t vertexCount = 0;
            if (!readValueAt(path, cursor, vertexCount))
                return 0;
            cursor += sizeof(uint32_t) +
                static_cast<uint64_t>(vertexCount) * SERIALIZED_VERTEX_SIZE;
        }
        return cursor;
    }

    bool allTileSectionsRead(
        const ScopedTerrainTestFile& file,
        const terrain::TileIndexEntry& entry)
    {
        std::vector<float> heights;
        terrain::TileWeightMapData weights;
        std::array<terrain::TileLODData, terrain::TERRAIN_LOD_COUNT> lodData;
        std::vector<uint8_t> holes;
        terrain::CaveSDFData caves;
        return terrain::TerrainSerializer::readTileHeights(file.string(), entry, heights) &&
               terrain::TerrainSerializer::readTileWeights(file.string(), entry, weights) &&
               terrain::TerrainSerializer::readTileLODData(file.string(), entry, lodData) &&
               terrain::TerrainSerializer::readTileHoleMask(file.string(), entry, holes) &&
               terrain::TerrainSerializer::readTileCaveData(file.string(), entry, caves);
    }
}

TEST_SUITE("TerrainSerializerValidation")
{
    TEST_CASE("writer-produced files load at every supported resolution")
    {
        for (const auto resolution : {
                 terrain::TileResolution::Low,
                 terrain::TileResolution::Medium,
                 terrain::TileResolution::High})
        {
            CAPTURE(static_cast<uint32_t>(resolution));
            ScopedTerrainTestFile file("positive");
            REQUIRE(saveValidationTerrain(file, resolution));

            TerrainFileSnapshot snapshot;
            REQUIRE(readTerrainTestSnapshot(file.string(), snapshot));
            REQUIRE(snapshot.index.size() == 1);
            CHECK(allTileSectionsRead(file, snapshot.index.front()));
        }
    }

    TEST_CASE("height count is bounded before allocation and zero remains legal")
    {
        ScopedTerrainTestFile file("height-count");
        REQUIRE(saveValidationTerrain(file, terrain::TileResolution::High));

        TerrainFileSnapshot snapshot;
        REQUIRE(readTerrainTestSnapshot(file.string(), snapshot));
        REQUIRE(snapshot.index.size() == 1);
        const auto entry = snapshot.index.front();

        std::vector<float> heights;
        REQUIRE(terrain::TerrainSerializer::readTileHeights(file.string(), entry, heights));
        CHECK(heights.size() == terrain::MAX_TILE_HEIGHT_SAMPLES);

        REQUIRE(writeValueAt<uint32_t>(
            file.path(), entry.heightDataOffset, terrain::MAX_TILE_HEIGHT_SAMPLES + 1u));
        CHECK_FALSE(terrain::TerrainSerializer::readTileHeights(file.string(), entry, heights));

        REQUIRE(writeValueAt<uint32_t>(file.path(), entry.heightDataOffset, 0u));
        REQUIRE(terrain::TerrainSerializer::readTileHeights(file.string(), entry, heights));
        CHECK(heights.empty());
    }

    TEST_CASE("hole count is bounded and truncated packed data is rejected before unpacking")
    {
        ScopedTerrainTestFile file("hole-count");
        REQUIRE(saveValidationTerrain(file, terrain::TileResolution::High));

        TerrainFileSnapshot snapshot;
        REQUIRE(readTerrainTestSnapshot(file.string(), snapshot));
        REQUIRE(snapshot.index.size() == 1);
        const auto entry = snapshot.index.front();
        REQUIRE(entry.holeMaskDataOffset != 0);

        std::vector<uint8_t> holes;
        REQUIRE(terrain::TerrainSerializer::readTileHoleMask(file.string(), entry, holes));
        CHECK(holes.size() == terrain::MAX_TILE_HOLE_QUADS);

        REQUIRE(writeValueAt<uint32_t>(
            file.path(), entry.holeMaskDataOffset, terrain::MAX_TILE_HOLE_QUADS + 1u));
        CHECK_FALSE(terrain::TerrainSerializer::readTileHoleMask(file.string(), entry, holes));

        REQUIRE(writeValueAt<uint32_t>(
            file.path(), entry.holeMaskDataOffset, std::numeric_limits<uint32_t>::max()));
        CHECK_FALSE(terrain::TerrainSerializer::readTileHoleMask(file.string(), entry, holes));

        REQUIRE(writeValueAt<uint32_t>(
            file.path(), entry.holeMaskDataOffset, terrain::MAX_TILE_HOLE_QUADS));
        std::filesystem::resize_file(
            file.path(), entry.holeMaskDataOffset + sizeof(uint32_t) + 1u);
        CHECK_FALSE(terrain::TerrainSerializer::readTileHoleMask(file.string(), entry, holes));
    }

    TEST_CASE("meshlet and index counts are bounded before vector allocation")
    {
        ScopedTerrainTestFile file("meshlet-counts");
        REQUIRE(saveValidationTerrain(file, terrain::TileResolution::Low));

        TerrainFileSnapshot snapshot;
        REQUIRE(readTerrainTestSnapshot(file.string(), snapshot));
        REQUIRE(snapshot.index.size() == 1);
        const auto entry = snapshot.index.front();
        REQUIRE(entry.meshletDataOffset != 0);

        std::array<terrain::TileLODData, terrain::TERRAIN_LOD_COUNT> lodData;
        REQUIRE(terrain::TerrainSerializer::readTileLODData(file.string(), entry, lodData));

        uint32_t meshletCount = 0;
        REQUIRE(readValueAt(file.path(), entry.meshletDataOffset, meshletCount));
        REQUIRE(meshletCount > 0);
        uint32_t meshletVertexCount = 0;
        REQUIRE(readValueAt(
            file.path(), entry.meshletDataOffset + sizeof(uint32_t), meshletVertexCount));
        uint32_t meshletPrimitiveCount = 0;
        REQUIRE(readValueAt(
            file.path(), entry.meshletDataOffset + 2u * sizeof(uint32_t), meshletPrimitiveCount));

        REQUIRE(writeValueAt<uint32_t>(
            file.path(), entry.meshletDataOffset, TEST_MAX_MESHLETS_PER_LOD + 1u));
        CHECK_FALSE(terrain::TerrainSerializer::readTileLODData(file.string(), entry, lodData));

        REQUIRE(writeValueAt<uint32_t>(file.path(), entry.meshletDataOffset, meshletCount));
        REQUIRE(writeValueAt<uint32_t>(
            file.path(),
            entry.meshletDataOffset + sizeof(uint32_t),
            meshletCount * resource::MAX_MESHLET_VERTICES + 1u));
        CHECK_FALSE(terrain::TerrainSerializer::readTileLODData(file.string(), entry, lodData));

        REQUIRE(writeValueAt<uint32_t>(
            file.path(),
            entry.meshletDataOffset + sizeof(uint32_t),
            meshletCount * resource::MAX_MESHLET_VERTICES));
        REQUIRE(writeValueAt<uint32_t>(
            file.path(),
            entry.meshletDataOffset + 2u * sizeof(uint32_t),
            meshletCount * resource::MAX_MESHLET_PRIMITIVES + 1u));
        CHECK_FALSE(terrain::TerrainSerializer::readTileLODData(file.string(), entry, lodData));

        REQUIRE(writeValueAt<uint32_t>(
            file.path(), entry.meshletDataOffset + sizeof(uint32_t), meshletVertexCount));
        REQUIRE(writeValueAt<uint32_t>(
            file.path(), entry.meshletDataOffset + 2u * sizeof(uint32_t), meshletPrimitiveCount));

        const uint64_t indexCountOffset =
            firstLODIndexCountOffset(file.path(), entry.meshletDataOffset);
        REQUIRE(indexCountOffset != 0);
        REQUIRE(writeValueAt<uint32_t>(
            file.path(), indexCountOffset, TEST_MAX_INDICES_PER_LOD + 1u));
        CHECK_FALSE(terrain::TerrainSerializer::readTileLODData(file.string(), entry, lodData));
    }

    TEST_CASE("index offsets and height byte ranges are checked against EOF")
    {
        ScopedTerrainTestFile file("index-offsets");
        REQUIRE(saveValidationTerrain(file, terrain::TileResolution::Low));

        TerrainFileSnapshot snapshot;
        REQUIRE(readTerrainTestSnapshot(file.string(), snapshot));
        REQUIRE(snapshot.index.size() == 1);
        const auto entry = snapshot.index.front();
        const uint64_t fileSize = std::filesystem::file_size(file.path());

        constexpr uint64_t HEIGHT_OFFSET_FIELD = 2u * sizeof(int32_t);
        constexpr uint64_t HEIGHT_SIZE_FIELD = HEIGHT_OFFSET_FIELD + sizeof(uint64_t);
        constexpr std::array<uint64_t, 4> OPTIONAL_OFFSET_FIELDS = {
            HEIGHT_SIZE_FIELD + sizeof(uint32_t),
            HEIGHT_SIZE_FIELD + sizeof(uint32_t) + sizeof(uint64_t),
            HEIGHT_SIZE_FIELD + sizeof(uint32_t) + 2u * sizeof(uint64_t),
            HEIGHT_SIZE_FIELD + sizeof(uint32_t) + 3u * sizeof(uint64_t)
        };
        const uint64_t indexBase = snapshot.indexTableOffset;

        REQUIRE(writeValueAt<uint64_t>(file.path(), indexBase + HEIGHT_OFFSET_FIELD, 0u));
        CHECK_FALSE(headerReads(file));
        REQUIRE(writeValueAt<uint64_t>(
            file.path(), indexBase + HEIGHT_OFFSET_FIELD, entry.heightDataOffset));

        REQUIRE(writeValueAt<uint32_t>(file.path(), indexBase + HEIGHT_SIZE_FIELD, 0u));
        REQUIRE(writeValueAt<uint64_t>(file.path(), indexBase + HEIGHT_OFFSET_FIELD, fileSize - 1u));
        CHECK(headerReads(file));
        REQUIRE(writeValueAt<uint64_t>(file.path(), indexBase + HEIGHT_OFFSET_FIELD, fileSize));
        CHECK_FALSE(headerReads(file));
        REQUIRE(writeValueAt<uint64_t>(
            file.path(), indexBase + HEIGHT_OFFSET_FIELD, std::numeric_limits<uint64_t>::max()));
        CHECK_FALSE(headerReads(file));

        REQUIRE(writeValueAt<uint64_t>(
            file.path(), indexBase + HEIGHT_OFFSET_FIELD, entry.heightDataOffset));
        const uint64_t exactRemaining = fileSize - entry.heightDataOffset;
        REQUIRE(exactRemaining <= std::numeric_limits<uint32_t>::max());
        REQUIRE(writeValueAt<uint32_t>(
            file.path(), indexBase + HEIGHT_SIZE_FIELD, static_cast<uint32_t>(exactRemaining)));
        CHECK(headerReads(file));
        REQUIRE(writeValueAt<uint32_t>(
            file.path(), indexBase + HEIGHT_SIZE_FIELD, static_cast<uint32_t>(exactRemaining + 1u)));
        CHECK_FALSE(headerReads(file));

        const std::array<uint64_t, 4> originalOptionalOffsets = {
            entry.weightDataOffset,
            entry.meshletDataOffset,
            entry.holeMaskDataOffset,
            entry.caveSdfDataOffset
        };
        REQUIRE(writeValueAt<uint32_t>(
            file.path(), indexBase + HEIGHT_SIZE_FIELD, entry.heightDataSize));
        for (size_t i = 0; i < OPTIONAL_OFFSET_FIELDS.size(); ++i)
        {
            CAPTURE(i);
            const uint64_t field = indexBase + OPTIONAL_OFFSET_FIELDS[i];

            REQUIRE(writeValueAt<uint64_t>(file.path(), field, 0u));
            CHECK(headerReads(file));
            REQUIRE(writeValueAt<uint64_t>(file.path(), field, fileSize - 1u));
            CHECK(headerReads(file));
            REQUIRE(writeValueAt<uint64_t>(file.path(), field, fileSize));
            CHECK_FALSE(headerReads(file));
            REQUIRE(writeValueAt<uint64_t>(file.path(), field, fileSize + 1u));
            CHECK_FALSE(headerReads(file));
            REQUIRE(writeValueAt<uint64_t>(
                file.path(), field, std::numeric_limits<uint64_t>::max()));
            CHECK_FALSE(headerReads(file));
            REQUIRE(writeValueAt<uint64_t>(file.path(), field, originalOptionalOffsets[i]));
        }
        CHECK(headerReads(file));
    }
}

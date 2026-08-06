#include <doctest.h>

#include "test_terrain_serializer_fixture.hpp"

#include <terrain/TerrainCompression.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <vector>

TEST_SUITE("TerrainSerializer")
{
    TEST_CASE("full save header and tile readers round-trip all persisted sections")
    {
        const std::array<terrain::TileResolution, 3> resolutions = {
            terrain::TileResolution::Low,
            terrain::TileResolution::Medium,
            terrain::TileResolution::High
        };

        for (const auto resolution : resolutions)
        {
            CAPTURE(static_cast<uint32_t>(resolution));
            const auto config = makeTerrainTestConfig(resolution);
            const std::vector<terrain::TileCoord> coords = {{-1, 2}};
            const bool retainOriginalSdf = resolution == terrain::TileResolution::Low;
            auto grid = makePopulatedTerrainTestGrid(config, coords, retainOriginalSdf);
            const terrain::TerrainTile* original = grid->getTile(coords.front());
            REQUIRE(original != nullptr);
            REQUIRE_FALSE(original->lodLevels[0].meshlets.empty());
            REQUIRE(original->lodLevels[0].mainMeshletCount > 0);
            REQUIRE(original->caveData != nullptr);
            CHECK(original->caveData->originalSdfGrid.empty() == !retainOriginalSdf);

            ScopedTerrainTestFile file("roundtrip");
            const auto saveParams = makeTerrainTestSaveParams(file.string(), *grid, config);
            REQUIRE(terrain::TerrainSerializer::save(saveParams));

            TerrainFileSnapshot snapshot;
            REQUIRE(readTerrainTestSnapshot(file.string(), snapshot));
            REQUIRE(snapshot.index.size() == 1);

            CHECK(snapshot.header.versionMajor == terrain::TERRAIN_FORMAT_VERSION_MAJOR);
            CHECK(snapshot.header.versionMinor == terrain::TERRAIN_FORMAT_VERSION_MINOR);
            CHECK(snapshot.header.versionPatch == terrain::TERRAIN_FORMAT_VERSION_PATCH);
            CHECK(snapshot.header.tileCount == 1);
            CHECK(snapshot.header.resolution == static_cast<uint8_t>(resolution));
            CHECK(snapshot.header.worldTileSize == config.worldTileSize);
            CHECK(snapshot.header.maxHeight == config.maxHeight);
            CHECK(snapshot.header.minHeight == config.minHeight);
            CHECK(snapshot.header.skirtDepth == config.skirtDepth);
            CHECK(snapshot.header.gridMinX == coords.front().x);
            CHECK(snapshot.header.gridMinZ == coords.front().z);
            CHECK(snapshot.header.gridMaxX == coords.front().x);
            CHECK(snapshot.header.gridMaxZ == coords.front().z);
            CHECK(snapshot.header.materialPath == saveParams.materialPath);
            const terrain::TerrainFileHeader defaultHeader;
            for (size_t lod = 0; lod < terrain::TERRAIN_LOD_COUNT; ++lod)
                CHECK(snapshot.header.lodDistances[lod] == defaultHeader.lodDistances[lod]);
            CHECK(snapshot.header.physicsConfig.hasCollider);
            CHECK(snapshot.header.physicsConfig.collisionLayer == 6);
            CHECK(snapshot.header.physicsConfig.friction == 0.72f);
            CHECK(snapshot.header.physicsConfig.restitution == 0.08f);
            CHECK(snapshot.header.streamingConfig.enabled);
            CHECK(snapshot.header.streamingConfig.loadRadius == 384.0f);
            CHECK(snapshot.header.streamingConfig.unloadRadius == 448.0f);
            CHECK(snapshot.header.streamingConfig.maxLoadsPerFrame == 3);
            CHECK(snapshot.header.streamingConfig.maxUnloadsPerFrame == 2);

            const auto requiredFlags =
                terrain::TerrainFormatFlags::HAS_COMPRESSED_DATA |
                terrain::TerrainFormatFlags::HAS_WEIGHT_MAPS |
                terrain::TerrainFormatFlags::HAS_MESHLET_CACHE |
                terrain::TerrainFormatFlags::HAS_HOLE_MASK |
                terrain::TerrainFormatFlags::HAS_CAVE_DATA |
                terrain::TerrainFormatFlags::HAS_PHYSICS_DATA |
                terrain::TerrainFormatFlags::HAS_STREAMING_CONFIG;
            CHECK(static_cast<uint32_t>(snapshot.header.flags) ==
                  static_cast<uint32_t>(requiredFlags));

            const auto& entry = snapshot.index.front();
            CHECK(entry.coordX == coords.front().x);
            CHECK(entry.coordZ == coords.front().z);
            CHECK(entry.heightDataOffset != 0);
            CHECK(entry.heightDataSize != 0);
            CHECK(entry.weightDataOffset != 0);
            CHECK(entry.meshletDataOffset != 0);
            CHECK(entry.holeMaskDataOffset != 0);
            CHECK(entry.caveSdfDataOffset != 0);
            CHECK(snapshot.indexTableOffset != 0);

            std::vector<float> loadedHeights;
            REQUIRE(terrain::TerrainSerializer::readTileHeights(
                file.string(), entry, loadedHeights));
            REQUIRE(loadedHeights.size() == original->heightData.size());
            const auto heightRange =
                terrain::compression::computeHeightRange(original->heightData);
            const float heightTolerance =
                (heightRange.maxH - heightRange.minH) / 65535.0f;
            for (size_t i = 0; i < loadedHeights.size(); ++i)
            {
                CHECK(std::abs(loadedHeights[i] - original->heightData[i]) <=
                      heightTolerance + std::numeric_limits<float>::epsilon());
            }

            terrain::TileWeightMapData loadedWeights;
            REQUIRE(terrain::TerrainSerializer::readTileWeights(
                file.string(), entry, loadedWeights));
            CHECK(loadedWeights.resolution == original->weightMap.resolution);
            CHECK(loadedWeights.layerIndices == original->weightMap.layerIndices);
            REQUIRE(loadedWeights.layerWeights.size() ==
                    original->weightMap.layerWeights.size());
            for (size_t channel = 0; channel < loadedWeights.layerWeights.size(); ++channel)
            {
                REQUIRE(loadedWeights.layerWeights[channel].size() ==
                        original->weightMap.layerWeights[channel].size());
                for (size_t i = 0; i < loadedWeights.layerWeights[channel].size(); ++i)
                {
                    CHECK(std::abs(
                        loadedWeights.layerWeights[channel][i] -
                        original->weightMap.layerWeights[channel][i]) <= 1.0f / 255.0f);
                }
            }

            std::array<terrain::TileLODData, terrain::TERRAIN_LOD_COUNT> loadedLods;
            REQUIRE(terrain::TerrainSerializer::readTileLODData(
                file.string(), entry, loadedLods));
            for (size_t lod = 0; lod < terrain::TERRAIN_LOD_COUNT; ++lod)
            {
                CAPTURE(lod);
                CHECK(equalTerrainTestLODData(original->lodLevels[lod], loadedLods[lod]));
                CHECK(loadedLods[lod].mainMeshletCount == 0);
            }

            std::vector<uint8_t> loadedHoles;
            REQUIRE(terrain::TerrainSerializer::readTileHoleMask(
                file.string(), entry, loadedHoles));
            CHECK(loadedHoles == original->holeMask);

            terrain::CaveSDFData loadedCave;
            REQUIRE(terrain::TerrainSerializer::readTileCaveData(
                file.string(), entry, loadedCave));
            CHECK(equalTerrainTestCaveData(*original->caveData, loadedCave));
            CHECK(loadedCave.originalSdfGrid.empty() == !retainOriginalSdf);
        }
    }

    TEST_CASE("cave data without an original grid round-trips bit-exactly")
    {
        const auto config = makeTerrainTestConfig(terrain::TileResolution::Low);
        const std::vector<terrain::TileCoord> coords = {{0, 0}};
        auto grid = makePopulatedTerrainTestGrid(config, coords, false);
        const terrain::TerrainTile* original = grid->getTile(coords.front());
        REQUIRE(original != nullptr);
        REQUIRE(original->caveData != nullptr);
        REQUIRE(original->caveData->originalSdfGrid.empty());

        ScopedTerrainTestFile file("cave-no-original");
        REQUIRE(terrain::TerrainSerializer::save(
            makeTerrainTestSaveParams(file.string(), *grid, config)));

        TerrainFileSnapshot snapshot;
        REQUIRE(readTerrainTestSnapshot(file.string(), snapshot));
        REQUIRE(snapshot.index.size() == 1);

        terrain::CaveSDFData loaded;
        REQUIRE(terrain::TerrainSerializer::readTileCaveData(
            file.string(), snapshot.index.front(), loaded));
        CHECK(equalTerrainTestCaveData(*original->caveData, loaded));
        CHECK(loaded.originalSdfGrid.empty());
    }

    TEST_CASE("absent tile sections preserve their reader-specific return conventions")
    {
        const terrain::TileIndexEntry absent{};
        ScopedTerrainTestFile unusedFile("absent");

        std::vector<float> heights = {42.0f};
        CHECK_FALSE(terrain::TerrainSerializer::readTileHeights(
            unusedFile.string(), absent, heights));

        terrain::TileWeightMapData weights;
        weights.initializeDefault(33);
        REQUIRE(weights.isInitialized());
        CHECK(terrain::TerrainSerializer::readTileWeights(
            unusedFile.string(), absent, weights));
        CHECK_FALSE(weights.isInitialized());

        std::vector<uint8_t> holes = {1, 1, 1};
        CHECK(terrain::TerrainSerializer::readTileHoleMask(
            unusedFile.string(), absent, holes));
        CHECK(holes.empty());

        std::array<terrain::TileLODData, terrain::TERRAIN_LOD_COUNT> lods;
        lods[0].vertices.resize(1);
        CHECK_FALSE(terrain::TerrainSerializer::readTileLODData(
            unusedFile.string(), absent, lods));
        CHECK(lods[0].vertices.size() == 1);

        terrain::CaveSDFData cave;
        cave.sdfGrid = {-1.0f};
        CHECK_FALSE(terrain::TerrainSerializer::readTileCaveData(
            unusedFile.string(), absent, cave));
        CHECK(cave.sdfGrid == std::vector<float>{-1.0f});
    }

    TEST_CASE("saving an empty grid succeeds without creating a file")
    {
        const auto config = makeTerrainTestConfig(terrain::TileResolution::Low);
        terrain::TerrainGrid emptyGrid(config);
        ScopedTerrainTestFile file("empty");

        CHECK_FALSE(terrain_test_fs::exists(file.path()));
        CHECK(terrain::TerrainSerializer::save(
            makeTerrainTestSaveParams(file.string(), emptyGrid, config)));
        CHECK_FALSE(terrain_test_fs::exists(file.path()));
    }
}

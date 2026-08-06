#include <doctest.h>

#include "test_terrain_serializer_fixture.hpp"

#include <asset/AssetReferenceScanner.hpp>

#include <array>
#include <string>
#include <vector>

// The scanner rewrites materialPath inside the .vfTerrain binary when a material is renamed,
// shifting every absolute tile offset by the length delta. It used to do that with hand-rolled
// byte offsets that had gone stale (lodDistances grew 4 -> 6 floats, the index entry 44 -> 52
// bytes, caveSdfDataOffset was never adjusted, the streaming block was ignored), which silently
// corrupted every file it touched. These cases pin the arithmetic to the serializer's own
// definitions.
namespace
{
    struct ScannerTerrainFixture
    {
        explicit ScannerTerrainFixture(const std::string& materialPath, bool streamingEnabled)
            : config(makeTerrainTestConfig(terrain::TileResolution::Low)),
              coords{{0, 0}, {1, 0}},
              grid(makePopulatedTerrainTestGrid(config, coords, true)),
              file("scanner")
        {
            auto params = makeTerrainTestSaveParams(file.string(), *grid, config, materialPath);
            params.streamingConfig.enabled = streamingEnabled;
            REQUIRE(terrain::TerrainSerializer::save(params));
            REQUIRE(readTerrainTestSnapshot(file.string(), snapshot));
            // The fixture punches holes and builds cave SDF data, so every optional tile
            // section — including the caveSdfDataOffset the old code forgot — is present.
            const auto* entry = findTerrainTestEntry(snapshot.index, coords.front());
            REQUIRE(entry != nullptr);
            REQUIRE(entry->caveSdfDataOffset != 0);
            REQUIRE(entry->holeMaskDataOffset != 0);
        }

        terrain::TerrainTileConfig config;
        std::vector<terrain::TileCoord> coords;
        std::unique_ptr<terrain::TerrainGrid> grid;
        ScopedTerrainTestFile file;
        TerrainFileSnapshot snapshot;
    };

    // Every tile section must still decode after the rewrite — that is what catches an index
    // table whose offsets were shifted by the wrong stride or not at all.
    void checkEveryTileSectionStillReads(const std::string& path, const TerrainFileSnapshot& snap,
                                         const terrain::TerrainGrid& grid)
    {
        for (const auto& entry : snap.index)
        {
            CAPTURE(entry.coordX);
            CAPTURE(entry.coordZ);
            const terrain::TerrainTile* original =
                grid.getTile(terrain::TileCoord{entry.coordX, entry.coordZ});
            REQUIRE(original != nullptr);

            std::vector<float> heights;
            REQUIRE(terrain::TerrainSerializer::readTileHeights(path, entry, heights));
            CHECK(heights.size() == original->heightData.size());

            terrain::TileWeightMapData weights;
            REQUIRE(terrain::TerrainSerializer::readTileWeights(path, entry, weights));
            CHECK(weights.layerIndices == original->weightMap.layerIndices);

            std::vector<uint8_t> holes;
            REQUIRE(terrain::TerrainSerializer::readTileHoleMask(path, entry, holes));
            CHECK(holes == original->holeMask);

            terrain::CaveSDFData cave;
            REQUIRE(terrain::TerrainSerializer::readTileCaveData(path, entry, cave));
            CHECK(equalTerrainTestCaveData(*original->caveData, cave));
        }
    }
}

TEST_SUITE("TerrainReferenceScanner")
{
    TEST_CASE("renaming a terrain material rewrites the header and keeps every tile readable")
    {
        const std::string oldPath = "materials/original.vfTerrainMat";
        const std::array<std::string, 3> newPaths = {
            "materials/original.vfTerrainMat/deeper/longer.vfTerrainMat",  // longer
            "m.vfTerrainMat",                                              // shorter
            std::string(oldPath.size(), 'x')                               // equal length
        };

        for (const bool streamingEnabled : {false, true})
        {
            for (const auto& newPath : newPaths)
            {
                CAPTURE(streamingEnabled);
                CAPTURE(newPath);

                ScannerTerrainFixture fixture(oldPath, streamingEnabled);
                const auto searchRoot = fixture.file.path().parent_path().string();

                const auto result = asset::AssetReferenceScanner::updateReferences(
                    oldPath, newPath, searchRoot);

                REQUIRE(result.referencingFiles.size() == 1);
                CHECK(result.failedFiles.empty());
                REQUIRE(result.updatedFiles.size() == 1);

                TerrainFileSnapshot rewritten;
                REQUIRE(readTerrainTestSnapshot(fixture.file.string(), rewritten));
                CHECK(rewritten.header.materialPath == newPath);
                CHECK(terrain::serializedHeaderSize(rewritten.header) == rewritten.indexTableOffset);
                REQUIRE(rewritten.index.size() == fixture.snapshot.index.size());

                checkEveryTileSectionStillReads(fixture.file.string(), rewritten, *fixture.grid);
            }
        }
    }

    TEST_CASE("a terrain referencing a different material is left alone")
    {
        ScannerTerrainFixture fixture("materials/original.vfTerrainMat", true);
        const auto searchRoot = fixture.file.path().parent_path().string();
        const auto before = readTerrainTestFileBytes(fixture.file.path());

        const auto result = asset::AssetReferenceScanner::updateReferences(
            "materials/unrelated.vfTerrainMat", "materials/renamed.vfTerrainMat", searchRoot);

        CHECK(result.referencingFiles.empty());
        CHECK(readTerrainTestFileBytes(fixture.file.path()) == before);
    }
}

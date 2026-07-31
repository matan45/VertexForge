#include <doctest.h>

#include <terrain/TerrainTile.hpp>
#include <terrain/TerrainTileGenerator.hpp>

#include <cstdint>
#include <vector>

namespace
{
    terrain::TerrainTile makeQuadraticTile(const terrain::TerrainTileConfig& config)
    {
        terrain::TerrainTile tile({0, 0}, config);
        const uint32_t vertexCount = config.getVertexCount();
        std::vector<float> heights(static_cast<size_t>(vertexCount) * vertexCount);
        for (uint32_t z = 0; z < vertexCount; ++z)
        {
            for (uint32_t x = 0; x < vertexCount; ++x)
            {
                heights[static_cast<size_t>(z) * vertexCount + x] =
                    static_cast<float>(x * x + z * z);
            }
        }
        tile.initializeFromHeights(heights);
        return tile;
    }
}

TEST_SUITE("TerrainEdgeStitching")
{
    TEST_CASE("edge predicates and corner edge priority are stable")
    {
        terrain::TerrainTileConfig config;
        terrain::TerrainTileGenerator generator(config);
        constexpr uint32_t vertexCount = 33;

        CHECK(generator.isEdgeVertex(0, 12, vertexCount));
        CHECK(generator.isEdgeVertex(32, 12, vertexCount));
        CHECK(generator.isEdgeVertex(12, 0, vertexCount));
        CHECK(generator.isEdgeVertex(12, 32, vertexCount));
        CHECK_FALSE(generator.isEdgeVertex(12, 12, vertexCount));

        CHECK(generator.isCornerVertex(0, 0, vertexCount));
        CHECK_FALSE(generator.isCornerVertex(0, 12, vertexCount));
        CHECK(generator.getEdgeForVertex(0, 0, vertexCount) == terrain::TileEdge::South);
        CHECK(generator.getEdgeForVertex(32, 32, vertexCount) == terrain::TileEdge::North);

        CHECK(generator.getEdgeVertexIndex(7, 32, vertexCount, terrain::TileEdge::North) == 7);
        CHECK(generator.getEdgeVertexIndex(32, 9, vertexCount, terrain::TileEdge::East) == 9);
    }

    TEST_CASE("quadratic heights expose interpolation on all four stitched edges")
    {
        terrain::TerrainTileConfig config;
        config.resolution = terrain::TileResolution::Low;
        terrain::TerrainTileGenerator generator(config);
        auto tile = makeQuadraticTile(config);
        constexpr uint32_t vertexCount = 33;

        SUBCASE("South")
        {
            tile.setNeighbor(terrain::TileEdge::South, {0, -1});
            CHECK(generator.getStitchedHeight(tile, 1, 0, vertexCount, 0) == 2.0f);
            CHECK(tile.getHeight(1, 0) == 1.0f);
        }

        SUBCASE("North")
        {
            tile.setNeighbor(terrain::TileEdge::North, {0, 1});
            CHECK(generator.getStitchedHeight(tile, 1, 32, vertexCount, 0) == 1026.0f);
            CHECK(tile.getHeight(1, 32) == 1025.0f);
        }

        SUBCASE("West")
        {
            tile.setNeighbor(terrain::TileEdge::West, {-1, 0});
            CHECK(generator.getStitchedHeight(tile, 0, 1, vertexCount, 0) == 2.0f);
            CHECK(tile.getHeight(0, 1) == 1.0f);
        }

        SUBCASE("East")
        {
            tile.setNeighbor(terrain::TileEdge::East, {1, 0});
            CHECK(generator.getStitchedHeight(tile, 32, 1, vertexCount, 0) == 1026.0f);
            CHECK(tile.getHeight(32, 1) == 1025.0f);
        }
    }

    TEST_CASE("stitching leaves no-neighbor aligned and coarsest vertices unchanged")
    {
        terrain::TerrainTileConfig config;
        config.resolution = terrain::TileResolution::Low;
        terrain::TerrainTileGenerator generator(config);

        SUBCASE("no neighbor")
        {
            auto tile = makeQuadraticTile(config);
            CHECK(generator.getStitchedHeight(tile, 1, 0, 33, 0) == tile.getHeight(1, 0));
        }

        SUBCASE("already aligned to the coarser grid")
        {
            auto tile = makeQuadraticTile(config);
            tile.setNeighbor(terrain::TileEdge::South, {0, -1});
            CHECK(generator.getStitchedHeight(tile, 2, 0, 33, 0) == tile.getHeight(2, 0));
        }

        SUBCASE("coarsest LOD")
        {
            auto tile = makeQuadraticTile(config);
            tile.setNeighbor(terrain::TileEdge::South, {0, -1});
            constexpr uint32_t coarsestLOD = terrain::TERRAIN_LOD_COUNT - 1;
            constexpr uint32_t skip = 1u << coarsestLOD;
            constexpr uint32_t lodVertexCount = (33u - 1u) / skip + 1u;
            CHECK(generator.getStitchedHeight(tile, 1, 0, lodVertexCount, coarsestLOD)
                  == tile.getHeight(32, 0));
        }
    }
}

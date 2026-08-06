#include <doctest.h>

#include <terrain/BrushFalloff.hpp>
#include <terrain/BrushSampler.hpp>
#include <terrain/BrushTypes.hpp>
#include <terrain/PaintBrushTypes.hpp>

#include <cstdint>

TEST_SUITE("TerrainBrushMath")
{
    TEST_CASE("brush falloff curves pin their endpoints and midpoint")
    {
        using terrain::BrushFalloff;

        CHECK(terrain::applyFalloff(0.0f, BrushFalloff::Constant) == 1.0f);
        CHECK(terrain::applyFalloff(0.5f, BrushFalloff::Constant) == 1.0f);
        CHECK(terrain::applyFalloff(1.0f, BrushFalloff::Constant) == 1.0f);

        CHECK(terrain::applyFalloff(0.0f, BrushFalloff::Linear) == 1.0f);
        CHECK(terrain::applyFalloff(0.5f, BrushFalloff::Linear) == 0.5f);
        CHECK(terrain::applyFalloff(1.0f, BrushFalloff::Linear) == 0.0f);

        CHECK(terrain::applyFalloff(0.0f, BrushFalloff::Smooth) == 1.0f);
        CHECK(terrain::applyFalloff(0.5f, BrushFalloff::Smooth) == 0.5f);
        CHECK(terrain::applyFalloff(1.0f, BrushFalloff::Smooth) == 0.0f);

        CHECK(terrain::applyFalloff(0.0f, BrushFalloff::Sharp) == 1.0f);
        CHECK(terrain::applyFalloff(0.5f, BrushFalloff::Sharp) == 0.75f);
        CHECK(terrain::applyFalloff(1.0f, BrushFalloff::Sharp) == 0.0f);
    }

    TEST_CASE("brush sampling includes every tile touched by an inclusive AABB")
    {
        const auto brushTiles =
            terrain::BrushSampler::getAffectedTiles(glm::vec2(32.0f, 32.0f), 1.0f, 32.0f);
        REQUIRE(brushTiles.size() == 4);
        CHECK(brushTiles[0] == terrain::TileCoord(0, 0));
        CHECK(brushTiles[1] == terrain::TileCoord(1, 0));
        CHECK(brushTiles[2] == terrain::TileCoord(0, 1));
        CHECK(brushTiles[3] == terrain::TileCoord(1, 1));

        const auto segmentTiles = terrain::BrushSampler::getAffectedTilesForSegment(
            glm::vec2(-1.0f, 16.0f), glm::vec2(65.0f, 16.0f), 1.0f, 32.0f);
        REQUIRE(segmentTiles.size() == 4);
        CHECK(segmentTiles[0] == terrain::TileCoord(-1, 0));
        CHECK(segmentTiles[1] == terrain::TileCoord(0, 0));
        CHECK(segmentTiles[2] == terrain::TileCoord(1, 0));
        CHECK(segmentTiles[3] == terrain::TileCoord(2, 0));
    }

    TEST_CASE("brush parameter validation clamps only documented fields")
    {
        terrain::BrushParams params;
        params.radius = -4.0f;
        params.strength = 200.0f;
        params.rampWidth = -3.0f;
        params.rampFalloff = -2.0f;
        params.hydraulicRainRate = 3.0f;
        params.hydraulicSedimentCapacity = 0.0f;
        params.hydraulicEvaporation = -1.0f;
        params.hydraulicHardness = 2.0f;
        params.hydraulicSmoothing = -2.0f;
        params.hydraulicIterations = 0;

        params.validate();

        CHECK(params.radius == 0.1f);
        CHECK(params.strength == 100.0f);
        CHECK(params.rampWidth == -3.0f);
        CHECK(params.rampFalloff == -2.0f);
        CHECK(params.hydraulicRainRate == 2.0f);
        CHECK(params.hydraulicSedimentCapacity == 0.1f);
        CHECK(params.hydraulicEvaporation == 0.0f);
        CHECK(params.hydraulicHardness == 1.0f);
        CHECK(params.hydraulicSmoothing == 0.0f);
        CHECK(params.hydraulicIterations == 1);
    }

    TEST_CASE("paint parameter validation clamps radius strength and opacity")
    {
        terrain::PaintBrushParams params;
        params.radius = 0.0f;
        params.strength = -1.0f;
        params.opacity = 2.0f;
        params.activeLayer = 17;

        params.validate();

        CHECK(params.radius == 0.1f);
        CHECK(params.strength == 0.0f);
        CHECK(params.opacity == 1.0f);
        CHECK(params.activeLayer == 17);
    }

    TEST_CASE("square brush shape keeps its serialized integer value")
    {
        CHECK(static_cast<uint8_t>(terrain::BrushShape::Square) == 1);
    }
}

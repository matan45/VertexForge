#include <doctest.h>

#include <terrain/BrushFalloff.hpp>
#include <terrain/HeightBrushApplicator.hpp>
#include <terrain/HoleBrushApplicator.hpp>
#include <terrain/TerrainWeightMap.hpp>
#include <terrain/WeightBrushApplicator.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

// VK-1624 runtime terrain edits. These cover terrain::HeightBrushApplicator -- the CPU height
// brush the script natives run -- plus the two conventions the service layer relies on when it
// drives the pre-existing hole and weight applicators.
//
// The whole point of putting the deform loop in Terrain.dll rather than in TerrainService is that
// it can be tested here: no Services, no EnTT, no Vulkan, no GPU.

namespace
{
    constexpr uint32_t TILE_VERTS = 3; // 3x3 vertices, spacing 1 -> world 0..2 on both axes

    terrain::HeightBrushApplicator::ApplyParams makeHeightParams()
    {
        terrain::HeightBrushApplicator::ApplyParams params{};
        params.brushCenter = glm::vec2(1.0f, 1.0f); // the centre vertex
        params.tileWorldOrigin = glm::vec2(0.0f);
        params.brushRadius = 1.0f;
        params.vertexSpacing = 1.0f;
        params.verticesPerSide = TILE_VERTS;
        params.falloff = terrain::BrushFalloff::Constant;
        params.shape = terrain::BrushShape::Circle;
        params.mode = terrain::HeightEditMode::Add;
        params.amount = 1.0f;
        params.minHeight = -1000.0f;
        params.maxHeight = 1000.0f;
        return params;
    }

    std::vector<float> makePlane(float value)
    {
        return std::vector<float>(static_cast<size_t>(TILE_VERTS) * TILE_VERTS, value);
    }

    float at(const std::vector<float>& plane, uint32_t x, uint32_t z)
    {
        return plane[static_cast<size_t>(z) * TILE_VERTS + x];
    }

    // Count vertices that moved off the uniform starting value.
    int changedCount(const std::vector<float>& plane, float original)
    {
        int count = 0;
        for (float h : plane)
        {
            if (h != original)
                ++count;
        }
        return count;
    }
}

TEST_SUITE("TerrainRuntimeEdit")
{
    // 1. The falloff curve is the declared single source of truth shared by the CPU applicators
    //    and brush_compute.glsl / brush_influence.glsl. These are the shader's formulae
    //    transcribed by hand: if someone edits BrushFalloff.hpp, this is the trip-wire.
    TEST_CASE("falloff curves match the terrain brush shader")
    {
        const float samples[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};

        for (float t : samples)
        {
            CHECK(terrain::applyFalloff(t, terrain::BrushFalloff::Constant) == doctest::Approx(1.0f));
            CHECK(terrain::applyFalloff(t, terrain::BrushFalloff::Linear) == doctest::Approx(1.0f - t));
            CHECK(terrain::applyFalloff(t, terrain::BrushFalloff::Smooth)
                  == doctest::Approx(1.0f - t * t * (3.0f - 2.0f * t)));
            CHECK(terrain::applyFalloff(t, terrain::BrushFalloff::Sharp)
                  == doctest::Approx(1.0f - t * t));
        }
    }

    // 2. The rim is EXCLUSIVE (dist >= 1.0 skips), matching the shader and both sibling
    //    applicators. A vertex sitting exactly on the brush edge must not move -- which is what
    //    makes a radius-1.0 brush on a spacing-1.0 grid touch precisely one vertex.
    TEST_CASE("brush footprint honours the shape metric and the exclusive rim")
    {
        SUBCASE("circle, radius exactly one spacing: only the centre vertex")
        {
            auto plane = makePlane(0.0f);
            auto params = makeHeightParams();
            REQUIRE(terrain::HeightBrushApplicator::apply(plane, params));

            CHECK(at(plane, 1, 1) == 1.0f);
            CHECK(changedCount(plane, 0.0f) == 1);
        }

        SUBCASE("circle, radius 1.5: centre, orthogonals and diagonals")
        {
            auto plane = makePlane(0.0f);
            auto params = makeHeightParams();
            params.brushRadius = 1.5f; // diagonals are sqrt(2)/1.5 = 0.943 < 1
            REQUIRE(terrain::HeightBrushApplicator::apply(plane, params));

            CHECK(changedCount(plane, 0.0f) == 9);
        }

        SUBCASE("square uses Chebyshev distance, so its rim excludes the diagonals too")
        {
            auto plane = makePlane(0.0f);
            auto params = makeHeightParams();
            params.shape = terrain::BrushShape::Square;
            REQUIRE(terrain::HeightBrushApplicator::apply(plane, params));

            // max(|dx|,|dz|) is 1.0 for BOTH orthogonals and diagonals at radius 1.0.
            CHECK(changedCount(plane, 0.0f) == 1);

            auto wide = makePlane(0.0f);
            params.brushRadius = 1.5f;
            REQUIRE(terrain::HeightBrushApplicator::apply(wide, params));
            CHECK(changedCount(wide, 0.0f) == 9);
        }
    }

    // 3. Add is a DELTA scaled by influence -- not a set, and not scaled by any deltaTime. The
    //    non-zero starting height is what actually distinguishes the two.
    TEST_CASE("Add applies amount * influence on top of the existing height")
    {
        auto plane = makePlane(3.0f);
        auto params = makeHeightParams();
        params.falloff = terrain::BrushFalloff::Linear;
        params.brushRadius = 2.0f;
        params.amount = 4.0f;

        REQUIRE(terrain::HeightBrushApplicator::apply(plane, params));

        // Centre: t = 0 -> influence 1 -> +4. Exact: every term is representable.
        CHECK(at(plane, 1, 1) == 7.0f);
        // Orthogonal: t = 1/2 -> influence 0.5 -> +2.
        CHECK(at(plane, 2, 1) == 5.0f);
        CHECK(at(plane, 1, 0) == 5.0f);
        // Diagonal: t = sqrt(2)/2 -> influence 1 - 0.7071.
        const float diagonalInfluence = 1.0f - std::sqrt(2.0f) / 2.0f;
        CHECK(at(plane, 2, 2) == doctest::Approx(3.0f + 4.0f * diagonalInfluence));
    }

    // 4. Set with a Constant falloff must land EXACTLY on the target -- glm::mix(x, y, 1.0f)
    //    returns y bit-exactly, so no epsilon creeps into a scripted flatten.
    TEST_CASE("Set with a constant falloff reaches the target exactly")
    {
        auto plane = makePlane(3.0f);
        auto params = makeHeightParams();
        params.mode = terrain::HeightEditMode::Set;
        params.brushRadius = 5.0f; // covers the whole 3x3 tile
        params.amount = 7.5f;

        REQUIRE(terrain::HeightBrushApplicator::apply(plane, params));

        for (uint32_t z = 0; z < TILE_VERTS; ++z)
        {
            for (uint32_t x = 0; x < TILE_VERTS; ++x)
                CHECK(at(plane, x, z) == 7.5f);
        }
    }

    // 5. Set is a feathered mix, not a hard stamp: only the centre reaches the target.
    TEST_CASE("Set feathers toward the target through the falloff")
    {
        auto plane = makePlane(3.0f);
        auto params = makeHeightParams();
        params.mode = terrain::HeightEditMode::Set;
        params.falloff = terrain::BrushFalloff::Smooth;
        params.brushRadius = 2.0f;
        params.amount = 7.5f;

        REQUIRE(terrain::HeightBrushApplicator::apply(plane, params));

        CHECK(at(plane, 1, 1) == 7.5f);          // t = 0 -> influence 1
        CHECK(at(plane, 2, 1) > 3.0f);           // strictly moved
        CHECK(at(plane, 2, 1) < 7.5f);           // but not all the way
        CHECK(at(plane, 2, 1) == doctest::Approx(5.25f)); // t = 0.5 -> influence 0.5
    }

    // 6. Clamping is the LAST operation, exactly as the shader's single trailing clamp is, so a
    //    run of scripted edits cannot walk the surface outside the tile's authored range.
    TEST_CASE("height is clamped to the tile range after the edit")
    {
        SUBCASE("upper bound")
        {
            auto plane = makePlane(3.0f);
            auto params = makeHeightParams();
            params.brushRadius = 5.0f;
            params.amount = 1000.0f;
            params.maxHeight = 50.0f;

            REQUIRE(terrain::HeightBrushApplicator::apply(plane, params));
            for (float h : plane)
                CHECK(h == 50.0f);
        }

        SUBCASE("lower bound")
        {
            auto plane = makePlane(3.0f);
            auto params = makeHeightParams();
            params.brushRadius = 5.0f;
            params.amount = -1000.0f;
            params.minHeight = -5.0f;

            REQUIRE(terrain::HeightBrushApplicator::apply(plane, params));
            for (float h : plane)
                CHECK(h == -5.0f);
        }
    }

    // 7/8. "false" must mean "nothing changed", because the service uses it to decide whether the
    //      tile enters the pending batch -- i.e. whether it costs a seam weld and a Jolt rebuild.
    TEST_CASE("a brush that changes nothing reports false and leaves the plane untouched")
    {
        SUBCASE("brush centre far off the tile")
        {
            auto plane = makePlane(3.0f);
            const auto before = plane;
            auto params = makeHeightParams();
            params.brushCenter = glm::vec2(1000.0f, 1000.0f);

            CHECK_FALSE(terrain::HeightBrushApplicator::apply(plane, params));
            CHECK(plane == before);
        }

        SUBCASE("zero amount")
        {
            auto plane = makePlane(3.0f);
            const auto before = plane;
            auto params = makeHeightParams();
            params.brushRadius = 5.0f;
            params.amount = 0.0f;

            CHECK_FALSE(terrain::HeightBrushApplicator::apply(plane, params));
            CHECK(plane == before);
        }

        SUBCASE("non-positive radius is rejected rather than dividing by zero")
        {
            auto plane = makePlane(3.0f);
            const auto before = plane;
            auto params = makeHeightParams();
            params.brushRadius = 0.0f;

            CHECK_FALSE(terrain::HeightBrushApplicator::apply(plane, params));
            CHECK(plane == before);
        }
    }

    // 9. A streamed-out or not-yet-loaded tile has an empty or short plane. The runtime path
    //    deliberately does not load heights from disk on the script thread, so it hands those
    //    tiles straight to the applicator and relies on this guard.
    TEST_CASE("an undersized height plane is rejected without writing")
    {
        auto params = makeHeightParams();
        params.brushRadius = 5.0f;

        SUBCASE("empty")
        {
            std::vector<float> plane;
            CHECK_FALSE(terrain::HeightBrushApplicator::apply(plane, params));
            CHECK(plane.empty());
        }

        SUBCASE("shorter than verticesPerSide squared")
        {
            std::vector<float> plane(4, 3.0f); // needs 9
            const auto before = plane;
            CHECK_FALSE(terrain::HeightBrushApplicator::apply(plane, params));
            CHECK(plane == before);
        }
    }

    // 10. Set + Constant is a fixed point, so re-running a flatten is free. Note this does NOT
    //     generalise: with a partial falloff, mix() only converges toward the target, so a
    //     repeated Set keeps moving. Only the Constant case is idempotent.
    TEST_CASE("Set with a constant falloff is idempotent")
    {
        auto plane = makePlane(3.0f);
        auto params = makeHeightParams();
        params.mode = terrain::HeightEditMode::Set;
        params.brushRadius = 5.0f;
        params.amount = 7.5f;

        REQUIRE(terrain::HeightBrushApplicator::apply(plane, params));
        const auto afterFirst = plane;

        CHECK_FALSE(terrain::HeightBrushApplicator::apply(plane, params));
        CHECK(plane == afterFirst);
    }

    // 11. Holes are per-QUAD (quadsPerSide = verticesPerSide - 1) while heights are per-vertex.
    //     Getting this wrong in the service wrapper is the single easiest bug to write, so pin
    //     that the same brush covers a different set.
    TEST_CASE("hole edits are per-quad, not per-vertex")
    {
        const uint32_t quadsPerSide = TILE_VERTS - 1;
        std::vector<uint8_t> holeMask(static_cast<size_t>(quadsPerSide) * quadsPerSide, 0);

        terrain::HoleBrushApplicator::ApplyParams params{};
        params.brushCenter = glm::vec2(1.0f, 1.0f);
        params.tileWorldOrigin = glm::vec2(0.0f);
        params.brushRadius = 1.0f;
        params.vertexSpacing = 1.0f;
        params.quadsPerSide = quadsPerSide;
        params.falloff = terrain::BrushFalloff::Constant;
        params.shape = terrain::BrushShape::Circle;
        params.erase = false;

        REQUIRE(terrain::HoleBrushApplicator::apply(holeMask, params));

        // Quad centres sit at (0.5, 0.5) .. (1.5, 1.5), all sqrt(0.5) from the brush centre, so
        // the same radius-1.0 brush that moved exactly ONE vertex punches all FOUR quads.
        CHECK(holeMask.size() == 4);
        for (uint8_t quad : holeMask)
            CHECK(quad == 1);

        auto plane = makePlane(0.0f);
        REQUIRE(terrain::HeightBrushApplicator::apply(plane, makeHeightParams()));
        CHECK(changedCount(plane, 0.0f) == 1);
    }

    // 12. The service passes deltaTime = 1 to WeightBrushApplicator so a one-shot script paint
    //     applies an ABSOLUTE influence rather than the editor's per-second rate. Executable
    //     documentation of that convention.
    TEST_CASE("paint with deltaTime 1 makes strength the absolute influence")
    {
        terrain::TileWeightMapData weights;
        weights.initializeDefault(TILE_VERTS);

        terrain::WeightBrushApplicator::ApplyParams params{};
        params.brushCenter = glm::vec2(1.0f, 1.0f);
        params.tileWorldOrigin = glm::vec2(0.0f);
        params.brushRadius = 0.75f;
        params.brushStrength = 0.25f;
        params.brushOpacity = 1.0f;
        params.vertexSpacing = 1.0f;
        params.verticesPerSide = TILE_VERTS;
        params.falloff = terrain::BrushFalloff::Constant;
        params.shape = terrain::BrushShape::Circle;
        params.brushType = terrain::PaintBrushType::PaintLayer;
        params.activeLayer = 1;
        params.deltaTime = 1.0f;
        params.invert = false;

        REQUIRE(terrain::WeightBrushApplicator::apply(weights, params));

        // influence = falloff(1.0) * strength(0.25) * opacity(1.0) * dt(1.0) = 0.25
        CHECK(weights.getWeight(1, 1, 1) == doctest::Approx(0.25f));
        CHECK(weights.getWeight(0, 1, 1) == doctest::Approx(0.75f));

        float sum = 0.0f;
        for (uint32_t channel = 0; channel < terrain::WEIGHT_CHANNELS; ++channel)
            sum += weights.getWeight(channel, 1, 1);
        CHECK(sum == doctest::Approx(1.0f));
    }

    // 13. Because the brush is evaluated in WORLD space, two tiles sharing a seam compute the
    //     same value for the shared column with no communication. That is what makes the deferred
    //     seam weld a near-identity for a script edit rather than a correction.
    TEST_CASE("adjacent tiles agree on a shared seam column before any seam sync")
    {
        auto tileA = makePlane(3.0f);
        auto tileB = makePlane(3.0f);

        auto params = makeHeightParams();
        // A spans world x 0..2; B spans world x 2..4. A's last column and B's first column are
        // both world x = 2.
        params.brushCenter = glm::vec2(2.0f, 1.0f);
        params.brushRadius = 2.5f;
        params.falloff = terrain::BrushFalloff::Smooth;
        params.amount = -4.0f;

        params.tileWorldOrigin = glm::vec2(0.0f, 0.0f);
        REQUIRE(terrain::HeightBrushApplicator::apply(tileA, params));

        params.tileWorldOrigin = glm::vec2(2.0f, 0.0f);
        REQUIRE(terrain::HeightBrushApplicator::apply(tileB, params));

        for (uint32_t z = 0; z < TILE_VERTS; ++z)
        {
            CHECK(at(tileA, TILE_VERTS - 1, z) == at(tileB, 0, z));
            CHECK(at(tileA, TILE_VERTS - 1, z) < 3.0f); // and it actually moved
        }
    }
}

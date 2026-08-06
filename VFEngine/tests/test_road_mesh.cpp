#include <doctest.h>

#include <terrain/RoadMeshBuilder.hpp>
#include <terrain/SplineSampling.hpp>
#include <terrain/SplineTypes.hpp>

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <vector>

// VK-1621 — road ribbon generation along a terrain spline. Pure CPU: the builder takes its
// terrain heights through a callback, so everything here runs without a device, a TerrainGrid
// or a service bootstrap (same shape as test_terrain_spline_corridor.cpp).
namespace
{
    const terrain::RoadHeightFn flatTerrain = [](float, float) { return 0.0f; };

    // Ramps 0.1 m per metre of +Z. A road running along +X therefore has its left shoulder
    // below and its right shoulder above the centreline.
    const terrain::RoadHeightFn zRamp = [](float, float z) { return z * 0.1f; };

    [[nodiscard]] glm::vec3 triangleNormal(const resource::LODLevel& level, size_t triangle)
    {
        const glm::vec3& a = level.vertices[level.indices[triangle * 3 + 0]].position;
        const glm::vec3& b = level.vertices[level.indices[triangle * 3 + 1]].position;
        const glm::vec3& c = level.vertices[level.indices[triangle * 3 + 2]].position;
        return glm::cross(b - a, c - a);
    }

    [[nodiscard]] size_t triangleCount(const resource::LODLevel& level)
    {
        return level.indices.size() / 3;
    }

    [[nodiscard]] std::vector<glm::vec3> halfCircle(float radius, uint32_t steps)
    {
        std::vector<glm::vec3> points;
        points.reserve(steps + 1);
        for (uint32_t i = 0; i <= steps; ++i)
        {
            const float theta = glm::pi<float>() * static_cast<float>(i) / static_cast<float>(steps);
            points.emplace_back(radius * std::cos(theta), 0.0f, radius * std::sin(theta));
        }
        return points;
    }
}

TEST_SUITE("TerrainRoadMesh")
{
    TEST_CASE("arc-length resampling redistributes a chord-parameterised polyline evenly")
    {
        // sampleSplineCurve derives its step count from chord length, so its samples bunch up.
        // This input mimics that: a 1 m segment followed by a 9 m one.
        const std::vector<glm::vec3> input{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}};
        const auto curve = terrain::roaddetail::resampleByArcLength(input, 1.0f);

        REQUIRE(curve.points.size() == 11);
        CHECK(curve.totalLength == doctest::Approx(10.0f));

        for (size_t i = 1; i < curve.points.size(); ++i)
            CHECK(glm::length(curve.points[i] - curve.points[i - 1]) == doctest::Approx(1.0f));

        for (size_t i = 0; i < curve.arcLength.size(); ++i)
            CHECK(curve.arcLength[i] == doctest::Approx(static_cast<float>(i)));

        // Endpoints must be exact, not merely close — the road has to start and stop on the spline.
        CHECK(curve.points.front().x == doctest::Approx(0.0f));
        CHECK(curve.points.back().x == doctest::Approx(10.0f));
    }

    TEST_CASE("a straight road on flat terrain is an exact strip wound front-face-up")
    {
        const terrain::RoadProfile profile = terrain::makeDefaultRoadProfile(4.0f, 1.5f, 0.15f);
        const auto data = terrain::buildRoadMesh({{0.0f, 0.0f, 0.0f}, {20.0f, 0.0f, 0.0f}},
                                                 profile, 1000.0f, flatTerrain);

        REQUIRE(data.valid);
        REQUIRE(data.chunks.size() == 1);
        CHECK(data.totalLength == doctest::Approx(20.0f));
        CHECK(data.minTurnRadius == 0.0f);
        CHECK_FALSE(data.clamped);

        const terrain::RoadChunk& chunk = data.chunks[0];
        const resource::LODLevel& lod0 = chunk.lods[0];
        CHECK(chunk.ringCount == 21);
        CHECK(lod0.vertices.size() == 21u * 4u);
        CHECK(lod0.indices.size() == 20u * 3u * 6u);

        SUBCASE("every triangle faces +Y")
        {
            // The winding must match the terrain's own (TerrainTileGenerator.cpp:280-286,
            // "CCW winding for Vulkan front-face") or the road renders back-faced.
            for (size_t t = 0; t < triangleCount(lod0); ++t)
                CHECK(triangleNormal(lod0, t).y > 0.0f);
        }

        SUBCASE("normals point straight up on flat ground")
        {
            for (const resource::Vertex& v : lod0.vertices)
            {
                CHECK(v.normal.y == doctest::Approx(1.0f));
                CHECK(v.normal.x == doctest::Approx(0.0f));
                CHECK(v.normal.z == doctest::Approx(0.0f));
            }
        }

        SUBCASE("cross-section lies on the profile offsets, lifted by zOffset")
        {
            // Tangent +X gives right = (-t.z, 0, t.x) = +Z, so columns run -5.5 .. +5.5 in Z.
            CHECK(lod0.vertices[0].position.z == doctest::Approx(-5.5f));
            CHECK(lod0.vertices[1].position.z == doctest::Approx(-4.0f));
            CHECK(lod0.vertices[2].position.z == doctest::Approx(4.0f));
            CHECK(lod0.vertices[3].position.z == doctest::Approx(5.5f));
            for (uint32_t c = 0; c < 4; ++c)
            {
                CHECK(lod0.vertices[c].position.x == doctest::Approx(0.0f));
                CHECK(lod0.vertices[c].position.y == doctest::Approx(0.05f));
            }
            CHECK(lod0.vertices[4].position.x == doctest::Approx(1.0f)); // ring 1, column 0
        }

        SUBCASE("U comes from the profile, V from arc length in metres")
        {
            const float shoulderU = 1.5f / (2.0f * 4.0f);
            CHECK(lod0.vertices[0].texCoords.x == doctest::Approx(-shoulderU));
            CHECK(lod0.vertices[1].texCoords.x == doctest::Approx(0.0f));
            CHECK(lod0.vertices[2].texCoords.x == doctest::Approx(1.0f));
            CHECK(lod0.vertices[3].texCoords.x == doctest::Approx(1.0f + shoulderU));

            // uvTilingV defaults to 8 m per repeat, rings are 1 m apart.
            CHECK(lod0.vertices[0].texCoords.y == doctest::Approx(0.0f));
            CHECK(lod0.vertices[4 * 8].texCoords.y == doctest::Approx(1.0f));
            CHECK(lod0.vertices[4 * 20].texCoords.y == doctest::Approx(2.5f));
        }
    }

    TEST_CASE("V advances by a constant step per ring even on a curve")
    {
        const std::vector<terrain::SplineControlPoint> controlPoints{
            {{0.0f, 0.0f, 0.0f}}, {{10.0f, 0.0f, 0.0f}}, {{20.0f, 0.0f, 10.0f}}, {{30.0f, 0.0f, 10.0f}}};
        const std::vector<glm::vec3> samples = terrain::sampleSplineCurve(controlPoints, 0.5f);
        REQUIRE(samples.size() > 2);

        terrain::RoadProfile profile = terrain::makeDefaultRoadProfile();
        profile.ringSpacing = 1.0f;
        profile.uvTilingV = 8.0f;

        const auto data = terrain::buildRoadMesh(samples, profile, 1000.0f, flatTerrain);
        REQUIRE(data.valid);

        // Rings are placed at an exactly uniform arc length, so V is linear in ring index.
        // Driving V off the raw (chord-parameterised) spline samples would not be.
        const uint32_t columns = 4;
        uint32_t globalRings = 1;
        for (const terrain::RoadChunk& chunk : data.chunks)
            globalRings += chunk.ringCount - 1; // chunks share their boundary ring
        const float expectedStep = data.totalLength / static_cast<float>(globalRings - 1) / 8.0f;

        for (const terrain::RoadChunk& chunk : data.chunks)
        {
            const resource::LODLevel& lod0 = chunk.lods[0];
            for (size_t ring = 1; ring < lod0.vertices.size() / columns; ++ring)
            {
                const float delta = lod0.vertices[ring * columns].texCoords.y
                                  - lod0.vertices[(ring - 1) * columns].texCoords.y;
                CHECK(delta == doctest::Approx(expectedStep).epsilon(0.001));
            }
        }
    }

    TEST_CASE("a curve that merely grazes a tile boundary is not sheared into slivers")
    {
        // Catmull-Rom overshoot dips this nominally-flat road to z = -0.099, which puts ~10 rings
        // in tile row -1 even though the whole road fits in one 1000 m tile. Splitting there would
        // hand the renderer two chunks for no benefit; chunkMinTileFraction is what suppresses it.
        const std::vector<terrain::SplineControlPoint> controlPoints{
            {{0.0f, 0.0f, 0.0f}}, {{10.0f, 0.0f, 0.0f}}, {{20.0f, 0.0f, 10.0f}}, {{30.0f, 0.0f, 10.0f}}};
        const std::vector<glm::vec3> samples = terrain::sampleSplineCurve(controlPoints, 0.5f);

        const auto data = terrain::buildRoadMesh(samples, terrain::makeDefaultRoadProfile(),
                                                 1000.0f, flatTerrain);
        REQUIRE(data.valid);
        CHECK(data.chunks.size() == 1);

        // With the guard disabled the graze does split the road — proving the guard is load-bearing
        // and not just masking an unrelated behaviour.
        terrain::RoadProfile eager = terrain::makeDefaultRoadProfile();
        eager.chunkMinTileFraction = 0.0f;
        CHECK(terrain::buildRoadMesh(samples, eager, 1000.0f, flatTerrain).chunks.size() > 1);
    }

    TEST_CASE("shoulders feather onto the terrain while the road surface stays flat")
    {
        const terrain::RoadProfile profile = terrain::makeDefaultRoadProfile(4.0f, 1.5f, 0.15f);
        const auto data = terrain::buildRoadMesh({{0.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}},
                                                 profile, 1000.0f, zRamp);
        REQUIRE(data.valid);
        const auto& v = data.chunks[0].lods[0].vertices;

        // Centre height is terrain at z=0 -> 0. Surface columns share it (flatCrossSection),
        // shoulder columns (terrainBlend = 1) sit on the terrain under them.
        CHECK(v[0].position.y == doctest::Approx(-0.55f + 0.05f));
        CHECK(v[1].position.y == doctest::Approx(0.05f));
        CHECK(v[2].position.y == doctest::Approx(0.05f));
        CHECK(v[3].position.y == doctest::Approx(0.55f + 0.05f));
    }

    TEST_CASE("flatCrossSection off drapes every column on the terrain")
    {
        terrain::RoadProfile profile = terrain::makeDefaultRoadProfile(4.0f, 1.5f, 0.15f);
        profile.flatCrossSection = false;

        const auto data = terrain::buildRoadMesh({{0.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}},
                                                 profile, 1000.0f, zRamp);
        REQUIRE(data.valid);
        const auto& v = data.chunks[0].lods[0].vertices;

        CHECK(v[1].position.y == doctest::Approx(-0.40f + 0.05f));
        CHECK(v[2].position.y == doctest::Approx(0.40f + 0.05f));
    }

    TEST_CASE("the road chunks at terrain tile boundaries and shares the boundary ring")
    {
        const terrain::RoadProfile profile = terrain::makeDefaultRoadProfile();
        const auto data = terrain::buildRoadMesh({{0.0f, 0.0f, 0.0f}, {20.0f, 0.0f, 0.0f}},
                                                 profile, 8.0f, flatTerrain);

        REQUIRE(data.valid);
        REQUIRE(data.chunks.size() == 3);

        CHECK(data.chunks[0].tile.x == 0);
        CHECK(data.chunks[1].tile.x == 1);
        CHECK(data.chunks[2].tile.x == 2);
        CHECK(data.chunks[0].origin.x == doctest::Approx(0.0f));
        CHECK(data.chunks[1].origin.x == doctest::Approx(8.0f));
        CHECK(data.chunks[2].origin.x == doctest::Approx(16.0f));

        CHECK(data.chunks[0].ringCount == 9);
        CHECK(data.chunks[1].ringCount == 9);
        CHECK(data.chunks[2].ringCount == 5);

        SUBCASE("every quad row belongs to exactly one chunk")
        {
            uint32_t quadRows = 0;
            for (const terrain::RoadChunk& chunk : data.chunks)
                quadRows += chunk.ringCount - 1;
            CHECK(quadRows == 20); // == ringCount - 1 for the whole road: no gap, no overlap
        }

        SUBCASE("the shared ring lands on the same world position in both chunks")
        {
            const resource::LODLevel& a = data.chunks[0].lods[0];
            const resource::LODLevel& b = data.chunks[1].lods[0];
            for (uint32_t c = 0; c < 4; ++c)
            {
                const glm::vec3 endOfA = a.vertices[(9 - 1) * 4 + c].position + data.chunks[0].origin;
                const glm::vec3 startOfB = b.vertices[c].position + data.chunks[1].origin;
                CHECK(endOfA.x == doctest::Approx(startOfB.x));
                CHECK(endOfA.y == doctest::Approx(startOfB.y));
                CHECK(endOfA.z == doctest::Approx(startOfB.z));
            }
        }

        SUBCASE("V is rebased per chunk by a whole number of tiling repeats")
        {
            // Keeps V small enough for the .vfMesh's float16 UVs (VertexQuantization.hpp:138-139)
            // while staying continuous under a wrapped sampler.
            const float endOfA = data.chunks[0].lods[0].vertices[(9 - 1) * 4].texCoords.y;
            const float startOfB = data.chunks[1].lods[0].vertices[0].texCoords.y;
            const float wraps = endOfA - startOfB;
            CHECK(wraps == doctest::Approx(std::round(wraps)));
            CHECK(std::abs(startOfB) < 1.0f);
        }
    }

    TEST_CASE("the LOD chain decimates rings but keeps both ends and every column")
    {
        const terrain::RoadProfile profile = terrain::makeDefaultRoadProfile();
        const auto data = terrain::buildRoadMesh({{0.0f, 0.0f, 0.0f}, {20.0f, 0.0f, 0.0f}},
                                                 profile, 8.0f, flatTerrain);
        REQUIRE(data.valid);
        const terrain::RoadChunk& chunk = data.chunks[0]; // rings 0..8

        CHECK(chunk.lods[0].vertices.size() == 9u * 4u);
        CHECK(chunk.lods[1].vertices.size() == 5u * 4u);
        CHECK(chunk.lods[2].vertices.size() == 3u * 4u);
        CHECK(chunk.lods[3].vertices.size() == 2u * 4u);

        // Physics reads LOD 2 (PhysicsShapeFactory.cpp:168,222), so the coarse levels must stay a
        // faithful collision surface: same start, same end, full width, still front-face-up.
        for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
        {
            const resource::LODLevel& level = chunk.lods[lod];
            REQUIRE(level.vertices.size() >= 8);

            const size_t lastRing = level.vertices.size() - 4;
            const size_t lastRing0 = chunk.lods[0].vertices.size() - 4;
            for (uint32_t c = 0; c < 4; ++c)
            {
                CHECK(level.vertices[c].position.x
                      == doctest::Approx(chunk.lods[0].vertices[c].position.x));
                CHECK(level.vertices[c].position.z
                      == doctest::Approx(chunk.lods[0].vertices[c].position.z));
                CHECK(level.vertices[lastRing + c].position.x
                      == doctest::Approx(chunk.lods[0].vertices[lastRing0 + c].position.x));
                CHECK(level.vertices[lastRing + c].position.z
                      == doctest::Approx(chunk.lods[0].vertices[lastRing0 + c].position.z));
            }

            for (size_t t = 0; t < triangleCount(level); ++t)
                CHECK(triangleNormal(level, t).y > 0.0f);
        }
    }

    TEST_CASE("a turn tighter than the road pinches instead of folding through itself")
    {
        // Radius 2 with a 5.5 m half-profile: the inner edge would cross the centre of the
        // turn and bowtie without the curvature clamp.
        const std::vector<glm::vec3> arc = halfCircle(2.0f, 512);

        terrain::RoadProfile profile = terrain::makeDefaultRoadProfile(4.0f, 1.5f);
        profile.ringSpacing = 0.25f;

        const auto data = terrain::buildRoadMesh(arc, profile, 1000.0f, flatTerrain);
        REQUIRE(data.valid);

        CHECK(data.clamped);
        // The circumradius estimator is exact on an arc, so this pins to the real radius rather
        // than to whatever a jittery finite-difference estimate happened to produce.
        CHECK(data.minTurnRadius == doctest::Approx(2.0f).epsilon(0.02));

        for (const terrain::RoadChunk& chunk : data.chunks)
        {
            for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
            {
                for (size_t t = 0; t < triangleCount(chunk.lods[lod]); ++t)
                {
                    // Columns clamped to the same limit coincide, giving zero-area triangles;
                    // what must never happen on flat terrain is a NEGATIVE (inverted) facing.
                    CHECK(triangleNormal(chunk.lods[lod], t).y >= 0.0f);
                }
            }
        }
    }

    TEST_CASE("a straight run entering a hairpin eases into the pinch instead of folding")
    {
        // The nastiest case for a per-ring clamp: 20 m of dead-straight road (no clamp) running
        // directly into a radius-1.5 U-turn (heavy clamp). Without the slope limiter the inner
        // edge jumps metres sideways in a single ring and the transition quad inverts.
        std::vector<glm::vec3> path;
        for (int i = 0; i <= 40; ++i)
            path.emplace_back(-20.0f + 0.5f * static_cast<float>(i), 0.0f, 0.0f);
        for (int i = 1; i <= 256; ++i)
        {
            const float theta = -glm::half_pi<float>()
                              + glm::pi<float>() * static_cast<float>(i) / 256.0f;
            path.emplace_back(1.5f * std::cos(theta), 0.0f, 1.5f + 1.5f * std::sin(theta));
        }

        terrain::RoadProfile profile = terrain::makeDefaultRoadProfile(4.0f, 1.5f);
        profile.ringSpacing = 0.25f;

        const auto data = terrain::buildRoadMesh(path, profile, 1000.0f, flatTerrain);
        REQUIRE(data.valid);
        CHECK(data.clamped);
        CHECK(data.minTurnRadius == doctest::Approx(1.5f).epsilon(0.05));

        for (const terrain::RoadChunk& chunk : data.chunks)
            for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
                for (size_t t = 0; t < triangleCount(chunk.lods[lod]); ++t)
                    CHECK(triangleNormal(chunk.lods[lod], t).y >= 0.0f);
    }

    TEST_CASE("a two-point spline stays on the linear path with no curvature")
    {
        const std::vector<terrain::SplineControlPoint> controlPoints{{{0.0f, 0.0f, 0.0f}},
                                                                     {{15.0f, 0.0f, 0.0f}}};
        // sampleSplineCurve is pure glm::mix for exactly two points (SplineSampling.hpp:39-49).
        const std::vector<glm::vec3> samples = terrain::sampleSplineCurve(controlPoints, 0.5f);

        const auto data = terrain::buildRoadMesh(samples, terrain::makeDefaultRoadProfile(),
                                                 1000.0f, flatTerrain);
        REQUIRE(data.valid);
        CHECK(data.totalLength == doctest::Approx(15.0f));
        CHECK(data.minTurnRadius == 0.0f);
        CHECK_FALSE(data.clamped);

        for (const resource::Vertex& v : data.chunks[0].lods[0].vertices)
            CHECK(v.normal.y == doctest::Approx(1.0f));
    }

    TEST_CASE("degenerate inputs produce no mesh rather than garbage")
    {
        const terrain::RoadProfile profile = terrain::makeDefaultRoadProfile();

        CHECK_FALSE(terrain::buildRoadMesh({}, profile, 32.0f, flatTerrain).valid);
        CHECK_FALSE(terrain::buildRoadMesh({{0.0f, 0.0f, 0.0f}}, profile, 32.0f, flatTerrain).valid);
        CHECK_FALSE(terrain::buildRoadMesh({{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
                                           profile, 0.0f, flatTerrain).valid);
        CHECK_FALSE(terrain::buildRoadMesh({{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
                                           profile, 32.0f, nullptr).valid);

        terrain::RoadProfile singleColumn;
        singleColumn.columns = {{0.0f, 0.5f, 0.0f, 0.0f}};
        CHECK_FALSE(terrain::buildRoadMesh({{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
                                           singleColumn, 32.0f, flatTerrain).valid);

        // A spline whose points all coincide has zero arc length.
        CHECK_FALSE(terrain::buildRoadMesh({{5.0f, 0.0f, 5.0f}, {5.0f, 0.0f, 5.0f}},
                                           profile, 32.0f, flatTerrain).valid);
    }

    TEST_CASE("SplineOps composes sculpt, paint and mesh into one apply")
    {
        using terrain::SplineOps;

        terrain::SplineOps ops = SplineOps::Sculpt | SplineOps::Mesh;
        CHECK(terrain::hasOp(ops, SplineOps::Sculpt));
        CHECK(terrain::hasOp(ops, SplineOps::Mesh));
        CHECK_FALSE(terrain::hasOp(ops, SplineOps::Paint));

        ops |= SplineOps::Paint;
        CHECK(terrain::hasOp(ops, SplineOps::Paint));

        CHECK_FALSE(terrain::hasOp(SplineOps::None, SplineOps::Sculpt));
        CHECK_FALSE(terrain::hasOp(SplineOps::None, SplineOps::Paint));
        CHECK_FALSE(terrain::hasOp(SplineOps::None, SplineOps::Mesh));
    }
}

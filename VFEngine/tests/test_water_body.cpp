// VK-1607: bounded water bodies (lakes / pools) additive to the singleton ocean.
//
// utilities/water/WaterBodyMath.hpp is the CPU twin of the tile decode and the ocean-clip discard in
// resources/shaders/water/water.glsl. Everything here is either that pure math or the scene
// serialization seam - no Vulkan, no registry, no editor.
//
// serialize/deserializeWaterBody are PRIVATE on SceneSerialization; the public seam is
// saveScene / loadSceneInto over a SceneGraphSystem, the same way test_billboard_serialization.cpp
// drives billboard serialization.

#include <doctest.h>

#include <water/WaterBodyMath.hpp>
#include <water/WaterTileGrid.hpp>
#include <serialization/SceneSerialization.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <components/Components.hpp>
#include <asset/AssetDatabase.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    water::WaterBodyDesc makeBody(float cx, float cz, float hx, float hz, float height,
                                  uint32_t bandMask = 0u, uint32_t physics = 1u)
    {
        water::WaterBodyDesc b;
        b.center = {cx, cz};
        b.halfExtents = {hx, hz};
        b.surfaceHeight = height;
        b.bandMask = bandMask;
        b.physicsEnabled = physics;
        return b;
    }

    fs::path waterBodyTestRoot()
    {
        return fs::temp_directory_path() / "vf_water_body_tests";
    }

    void resetWaterBodyTestRoot()
    {
        std::error_code ec;
        fs::remove_all(waterBodyTestRoot(), ec);
        fs::create_directories(waterBodyTestRoot(), ec);
        asset::AssetDatabase::instance().clear();
    }

    json readJson(const fs::path& path)
    {
        std::ifstream file(path);
        REQUIRE(file.is_open());
        json j;
        file >> j;
        return j;
    }
}

TEST_SUITE("WaterBody")
{
    TEST_CASE("containment is inclusive on every edge")
    {
        const auto body = makeBody(10.0f, -4.0f, 5.0f, 2.0f, 3.0f);

        SUBCASE("the centre is inside")
        {
            CHECK(water::containsXZ(body, {10.0f, -4.0f}));
        }

        SUBCASE("all four edges and all four corners are inside")
        {
            CHECK(water::containsXZ(body, {5.0f, -4.0f}));    // -x edge
            CHECK(water::containsXZ(body, {15.0f, -4.0f}));   // +x edge
            CHECK(water::containsXZ(body, {10.0f, -6.0f}));   // -z edge
            CHECK(water::containsXZ(body, {10.0f, -2.0f}));   // +z edge
            CHECK(water::containsXZ(body, {5.0f, -6.0f}));
            CHECK(water::containsXZ(body, {15.0f, -6.0f}));
            CHECK(water::containsXZ(body, {5.0f, -2.0f}));
            CHECK(water::containsXZ(body, {15.0f, -2.0f}));
        }

        SUBCASE("just outside on either axis is outside")
        {
            CHECK_FALSE(water::containsXZ(body, {15.001f, -4.0f}));
            CHECK_FALSE(water::containsXZ(body, {4.999f, -4.0f}));
            CHECK_FALSE(water::containsXZ(body, {10.0f, -1.999f}));
            CHECK_FALSE(water::containsXZ(body, {10.0f, -6.001f}));
        }

        SUBCASE("a zero-extent body still contains exactly its own centre")
        {
            const auto point = makeBody(1.0f, 2.0f, 0.0f, 0.0f, 0.0f);
            CHECK(water::containsXZ(point, {1.0f, 2.0f}));
            CHECK_FALSE(water::containsXZ(point, {1.001f, 2.0f}));
        }
    }

    TEST_CASE("clip rect matches centre +/- half extents and never inverts")
    {
        const glm::vec4 r = water::clipRect(makeBody(10.0f, -4.0f, 5.0f, 2.0f, 0.0f));
        CHECK(r.x == doctest::Approx(5.0f));
        CHECK(r.y == doctest::Approx(-6.0f));
        CHECK(r.z == doctest::Approx(15.0f));
        CHECK(r.w == doctest::Approx(-2.0f));

        // min <= max is what the shader's greaterThanEqual/lessThanEqual pair relies on; negative
        // half extents must clamp rather than produce a rectangle nothing can be inside.
        const glm::vec4 neg = water::clipRect(makeBody(0.0f, 0.0f, -3.0f, -1.0f, 0.0f));
        CHECK(neg.x <= neg.z);
        CHECK(neg.y <= neg.w);
    }

    TEST_CASE("overlap precedence is deterministic and independent of input order")
    {
        // Three bodies all covering the origin, deliberately given in a "wrong" order.
        std::vector<water::WaterBodyDesc> bodies = {
            makeBody(0.0f, 0.0f, 20.0f, 20.0f, 1.0f),   // big, low
            makeBody(0.0f, 0.0f, 2.0f, 2.0f, 5.0f),     // small, HIGH  <- should win
            makeBody(0.0f, 0.0f, 50.0f, 50.0f, 5.0f),   // huge, equally high
        };

        SUBCASE("the highest surface wins")
        {
            const int idx = water::findBodyAt(bodies.data(), bodies.size(), {0.0f, 0.0f});
            REQUIRE(idx >= 0);
            CHECK(bodies[static_cast<size_t>(idx)].surfaceHeight == doctest::Approx(5.0f));
            CHECK(idx == 1);   // the smaller of the two at height 5
        }

        SUBCASE("on an equal surface the smaller area wins")
        {
            std::vector<water::WaterBodyDesc> tie = {
                makeBody(0.0f, 0.0f, 50.0f, 50.0f, 2.0f),
                makeBody(0.0f, 0.0f, 3.0f, 3.0f, 2.0f),
            };
            CHECK(water::findBodyAt(tie.data(), tie.size(), {0.0f, 0.0f}) == 1);
        }

        SUBCASE("a full tie falls back to the lowest index")
        {
            std::vector<water::WaterBodyDesc> tie = {
                makeBody(0.0f, 0.0f, 4.0f, 4.0f, 2.0f),
                makeBody(0.0f, 0.0f, 4.0f, 4.0f, 2.0f),
            };
            CHECK(water::findBodyAt(tie.data(), tie.size(), {0.0f, 0.0f}) == 0);
        }

        SUBCASE("the winning BODY is the same under every permutation")
        {
            // The index moves with the permutation, but the body it names must not - otherwise a
            // registry reshuffle would silently change which surface a boat floats on.
            std::vector<water::WaterBodyDesc> perm = bodies;
            std::sort(perm.begin(), perm.end(),
                      [](const water::WaterBodyDesc& a, const water::WaterBodyDesc& b)
                      { return a.halfExtents.x < b.halfExtents.x; });

            do
            {
                const int idx = water::findBodyAt(perm.data(), perm.size(), {0.0f, 0.0f});
                REQUIRE(idx >= 0);
                const auto& won = perm[static_cast<size_t>(idx)];
                CHECK(won.surfaceHeight == doctest::Approx(5.0f));
                CHECK(won.halfExtents.x == doctest::Approx(2.0f));
            } while (std::next_permutation(perm.begin(), perm.end(),
                                           [](const water::WaterBodyDesc& a, const water::WaterBodyDesc& b)
                                           { return a.halfExtents.x < b.halfExtents.x; }));
        }

        SUBCASE("a point outside every body belongs to none of them")
        {
            CHECK(water::findBodyAt(bodies.data(), bodies.size(), {1000.0f, 0.0f}) == -1);
            CHECK(water::findBodyAt(nullptr, 0, {0.0f, 0.0f}) == -1);
        }
    }

    TEST_CASE("body-vs-ocean resolution hands over cleanly at the boundary")
    {
        const std::vector<water::WaterBodyDesc> bodies = {makeBody(0.0f, 0.0f, 10.0f, 10.0f, 40.0f)};
        bool found = false;

        SUBCASE("inside the body returns the body's surface, not the ocean's")
        {
            const float h = water::resolveSurfaceHeight(bodies.data(), bodies.size(), {0.0f, 0.0f},
                                                         2.0f, true, found);
            CHECK(found);
            CHECK(h == doctest::Approx(40.0f));
        }

        SUBCASE("one step outside falls through to the ocean")
        {
            const float h = water::resolveSurfaceHeight(bodies.data(), bodies.size(), {10.001f, 0.0f},
                                                         2.0f, true, found);
            CHECK(found);
            CHECK(h == doctest::Approx(2.0f));
        }

        SUBCASE("the body still answers when there is no ocean at all")
        {
            const float h = water::resolveSurfaceHeight(bodies.data(), bodies.size(), {0.0f, 0.0f},
                                                         0.0f, false, found);
            CHECK(found);
            CHECK(h == doctest::Approx(40.0f));
        }

        SUBCASE("no ocean and outside every body means NO water, not water at y = 0")
        {
            found = true;
            const float h = water::resolveSurfaceHeight(bodies.data(), bodies.size(), {500.0f, 0.0f},
                                                         0.0f, false, found);
            CHECK_FALSE(found);
            CHECK(h == doctest::Approx(0.0f));   // the value is meaningless; `found` is the answer
        }

        SUBCASE("no bodies at all leaves the ocean answering")
        {
            const float h = water::resolveSurfaceHeight(nullptr, 0, {0.0f, 0.0f}, -3.5f, true, found);
            CHECK(found);
            CHECK(h == doctest::Approx(-3.5f));
        }
    }

    TEST_CASE("a body tile is exactly the body's rectangle")
    {
        const auto tile = water::makeBodyTile(makeBody(10.0f, -4.0f, 5.0f, 2.0f, 7.5f, 0u));

        SUBCASE("origin is the min corner and the sizes are the full extents")
        {
            CHECK(tile.worldOriginAndSize.x == doctest::Approx(5.0f));    // min X
            CHECK(tile.worldOriginAndSize.z == doctest::Approx(-6.0f));   // min Z
            CHECK(tile.worldOriginAndSize.w == doctest::Approx(10.0f));   // size along X
            CHECK(tile.worldOriginAndSize.y == doctest::Approx(4.0f));    // size along Z
        }

        SUBCASE("the vertex shader's corner reconstruction lands on the clip rect")
        {
            // worldPos.xz = origin.xz + inPosition.xz * (sizeX, sizeZ), with inPosition in [0,1].
            const glm::vec4 rect = water::clipRect(makeBody(10.0f, -4.0f, 5.0f, 2.0f, 7.5f));
            CHECK(tile.worldOriginAndSize.x == doctest::Approx(rect.x));
            CHECK(tile.worldOriginAndSize.z == doctest::Approx(rect.y));
            CHECK(tile.worldOriginAndSize.x + tile.worldOriginAndSize.w == doctest::Approx(rect.z));
            CHECK(tile.worldOriginAndSize.z + tile.worldOriginAndSize.y == doctest::Approx(rect.w));
        }

        SUBCASE("height, LOD and the is-body flag survive the float round trip")
        {
            CHECK(tile.heightAndWave.x == doctest::Approx(7.5f));
            CHECK(tile.heightAndWave.z == doctest::Approx(0.0f));   // bodies always use LOD 0

            const uint32_t flags = static_cast<uint32_t>(tile.heightAndWave.w);
            CHECK((flags & water::WATER_TILE_IS_BODY) != 0u);
            CHECK((flags & water::WATER_TILE_BAND_MASK_BITS) == 0u);   // flat by default
        }

        SUBCASE("an authored band mask is carried through and never leaks into the flag bit")
        {
            const auto banded = water::makeBodyTile(makeBody(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0x5u));
            const uint32_t flags = static_cast<uint32_t>(banded.heightAndWave.w);
            CHECK((flags & water::WATER_TILE_BAND_MASK_BITS) == 0x5u);
            CHECK((flags & water::WATER_TILE_IS_BODY) != 0u);

            // Out-of-range bits must not survive into the is-body slot.
            const auto dirty = water::makeBodyTile(makeBody(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0xFFu));
            const uint32_t dirtyFlags = static_cast<uint32_t>(dirty.heightAndWave.w);
            CHECK(dirtyFlags == (water::WATER_TILE_BAND_MASK_BITS | water::WATER_TILE_IS_BODY));
        }

        SUBCASE("an ocean tile is distinguishable from a body tile")
        {
            CHECK((water::WATER_TILE_OCEAN_FLAGS & water::WATER_TILE_IS_BODY) == 0u);
            CHECK((water::WATER_TILE_OCEAN_FLAGS & water::WATER_TILE_BAND_MASK_BITS)
                  == water::WATER_TILE_BAND_MASK_BITS);
        }
    }

    TEST_CASE("the shared instance budget trims the coarsest LODs first")
    {
        SUBCASE("under budget nothing is touched")
        {
            uint32_t counts[water::WATER_TILE_LOD_COUNT] = {10, 20, 30, 40};
            CHECK(water::clampLodTileCounts(counts, water::MAX_WATER_GPU_INSTANCES) == 100u);
            CHECK(counts[0] == 10u);
            CHECK(counts[3] == 40u);
        }

        SUBCASE("exactly at budget nothing is touched")
        {
            uint32_t counts[water::WATER_TILE_LOD_COUNT] = {32, 32, 32, 32};
            CHECK(water::clampLodTileCounts(counts, 128u) == 128u);
            CHECK(counts[3] == 32u);
        }

        SUBCASE("over budget drops from the highest LOD down, and the sum matches the return")
        {
            uint32_t counts[water::WATER_TILE_LOD_COUNT] = {40, 40, 40, 40};   // 160
            const uint32_t kept = water::clampLodTileCounts(counts, 128u);
            CHECK(kept == 128u);
            CHECK(counts[3] == 8u);    // 32 of the 40 coarsest went first
            CHECK(counts[2] == 40u);
            CHECK(counts[0] == 40u);

            uint32_t sum = 0;
            for (uint32_t c : counts) sum += c;
            CHECK(sum == kept);
        }

        SUBCASE("body tiles at the head of LOD 0 survive even a massive overflow")
        {
            // 6 bodies prepended into LOD 0, then a full ocean grid on top of them.
            uint32_t counts[water::WATER_TILE_LOD_COUNT] = {6 + 100, 100, 100, 100};
            const uint32_t kept = water::clampLodTileCounts(counts, 128u);
            CHECK(kept == 128u);
            CHECK(counts[0] >= 6u);          // the bodies are never reached
            CHECK(counts[3] == 0u);
            CHECK(counts[2] == 0u);
        }

        SUBCASE("a budget smaller than LOD 0 alone still leaves a consistent sum")
        {
            uint32_t counts[water::WATER_TILE_LOD_COUNT] = {200, 10, 10, 10};
            const uint32_t kept = water::clampLodTileCounts(counts, 128u);
            CHECK(kept == 128u);
            uint32_t sum = 0;
            for (uint32_t c : counts) sum += c;
            CHECK(sum == kept);
        }
    }

    TEST_CASE("a water body survives a scene round trip")
    {
        resetWaterBodyTestRoot();

        scene::SceneGraphSystem source;
        auto& body = source.GetRoot().addOrReplaceComponent<components::WaterBodyComponent>();
        body.type = components::WaterBodyType::Pool;
        body.waterHeight = 12.25f;
        body.halfExtents = glm::vec2(7.5f, 3.25f);
        body.bandMask = 0x5u;
        body.physicsEnabled = false;   // non-default so a key mismatch is caught
        body.isActive = false;

        const fs::path scenePath = waterBodyTestRoot() / "Pool.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        const auto sceneJson = readJson(scenePath);
        REQUIRE(sceneJson["root"]["components"].contains("waterBody"));

        scene::SceneGraphSystem loaded;
        REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
        REQUIRE(loaded.GetRoot().hasComponent<components::WaterBodyComponent>());

        const auto& after = loaded.GetRoot().getComponent<components::WaterBodyComponent>();
        CHECK(after.type == components::WaterBodyType::Pool);
        CHECK(after.waterHeight == doctest::Approx(12.25f));
        CHECK(after.halfExtents.x == doctest::Approx(7.5f));
        CHECK(after.halfExtents.y == doctest::Approx(3.25f));
        CHECK(after.bandMask == 0x5u);
        CHECK(after.physicsEnabled == false);
        CHECK(after.isActive == false);
    }

    TEST_CASE("a water body block with missing keys keeps the component defaults")
    {
        resetWaterBodyTestRoot();

        // Save a body, then strip every key but one from its block - the shape a scene written
        // before a later field was added would have.
        scene::SceneGraphSystem source;
        source.GetRoot().addOrReplaceComponent<components::WaterBodyComponent>().waterHeight = 9.0f;

        const fs::path scenePath = waterBodyTestRoot() / "Sparse.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        json sceneJson = readJson(scenePath);
        sceneJson["root"]["components"]["waterBody"] = json::object();
        sceneJson["root"]["components"]["waterBody"]["waterHeight"] = 9.0f;
        {
            std::ofstream out(scenePath);
            REQUIRE(out.is_open());
            out << sceneJson.dump(2);
        }

        scene::SceneGraphSystem loaded;
        REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
        REQUIRE(loaded.GetRoot().hasComponent<components::WaterBodyComponent>());

        const auto& after = loaded.GetRoot().getComponent<components::WaterBodyComponent>();
        const components::WaterBodyComponent defaults{};
        CHECK(after.waterHeight == doctest::Approx(9.0f));
        CHECK(after.type == defaults.type);
        CHECK(after.halfExtents.x == doctest::Approx(defaults.halfExtents.x));
        CHECK(after.halfExtents.y == doctest::Approx(defaults.halfExtents.y));
        CHECK(after.bandMask == defaults.bandMask);
        CHECK(after.physicsEnabled == defaults.physicsEnabled);
        CHECK(after.isActive == defaults.isActive);
    }

    TEST_CASE("WaterBodyComponent is in OptionalComponents so duplication keeps it")
    {
        // The clone facility folds over exactly this type_list; a component missing from it is
        // silently dropped when an entity is duplicated (the VK-1606 BuoyancyComponent bug).
        constexpr bool present =
            entt::type_list_contains_v<components::OptionalComponents, components::WaterBodyComponent>;
        CHECK(present);
    }
}

#include <doctest.h>
#include <serialization/SceneSerialization.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <components/Components.hpp>
#include <asset/AssetDatabase.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

// VK-1621 — a generated road carries its source spline so the tool can re-open, edit and
// regenerate it after a scene reload. Splines themselves are session-only and a persistent terrain
// edit-layer sidecar is deferred to VK-1644..1648, so this component is the ONLY thing that makes
// a road re-editable; if it stops round-tripping, roads silently become one-shot.
namespace
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    fs::path roadTestRoot()
    {
        return fs::temp_directory_path() / "vf_road_spline_component_tests";
    }

    void resetRoadTestRoot()
    {
        std::error_code ec;
        fs::remove_all(roadTestRoot(), ec);
        fs::create_directories(roadTestRoot(), ec);
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

    void writeScene(const fs::path& path, json roadSpline)
    {
        json sceneJson;
        sceneJson["version"] = "1.0";
        sceneJson["root"] = {{"name", "Root"},
                             {"isActive", true},
                             {"components", {{"roadSpline", std::move(roadSpline)}}},
                             {"children", json::array()}};

        std::ofstream file(path);
        REQUIRE(file.is_open());
        file << sceneJson.dump(2);
    }
}

TEST_SUITE("RoadSplineComponent")
{
    TEST_CASE("a road's spline, profile and revision survive a save/load round trip")
    {
        resetRoadTestRoot();

        scene::SceneGraphSystem source;
        auto& road = source.GetRoot().addOrReplaceComponent<components::RoadSplineComponent>();
        road.controlPoints = {{0.0f, 0.0f, 0.0f}, {12.0f, 1.5f, 4.0f}, {30.0f, 0.0f, 4.0f}};
        road.params.ops = terrain::SplineOps::Sculpt | terrain::SplineOps::Mesh;
        road.params.corridorWidth = 7.5f;
        road.params.falloffWidth = 2.5f;
        road.params.embankmentHeight = 0.75f;
        road.params.paintLayer = 3;
        road.params.roadName = "CoastRoad";
        road.params.roadMaterialPath = "Assets/Materials/Asphalt.vfMat";
        road.params.roadCollider = true;
        road.params.road = terrain::makeDefaultRoadProfile(6.0f, 2.0f, 0.3f);
        road.params.road.ringSpacing = 1.5f;
        road.params.road.uvTilingV = 12.0f;
        road.params.road.zOffset = 0.08f;
        road.params.road.flatCrossSection = false;
        road.params.road.chunkMinTileFraction = 0.75f;
        road.splineId = 42;
        road.revision = 3;
        road.chunkCount = 5;

        const fs::path scenePath = roadTestRoot() / "CoastRoad.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        const auto sceneJson = readJson(scenePath);
        REQUIRE(sceneJson["root"]["components"].contains("roadSpline"));

        scene::SceneGraphSystem loaded;
        REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
        REQUIRE(loaded.GetRoot().hasComponent<components::RoadSplineComponent>());

        const auto& out = loaded.GetRoot().getComponent<components::RoadSplineComponent>();

        REQUIRE(out.controlPoints.size() == 3);
        CHECK(out.controlPoints[1].x == doctest::Approx(12.0f));
        CHECK(out.controlPoints[1].y == doctest::Approx(1.5f));
        CHECK(out.controlPoints[1].z == doctest::Approx(4.0f));
        CHECK(out.controlPoints[2].x == doctest::Approx(30.0f));

        CHECK(terrain::hasOp(out.params.ops, terrain::SplineOps::Sculpt));
        CHECK(terrain::hasOp(out.params.ops, terrain::SplineOps::Mesh));
        CHECK_FALSE(terrain::hasOp(out.params.ops, terrain::SplineOps::Paint));

        CHECK(out.params.corridorWidth == doctest::Approx(7.5f));
        CHECK(out.params.falloffWidth == doctest::Approx(2.5f));
        CHECK(out.params.embankmentHeight == doctest::Approx(0.75f));
        CHECK(out.params.paintLayer == 3);
        CHECK(out.params.roadName == "CoastRoad");
        CHECK(out.params.roadMaterialPath == "Assets/Materials/Asphalt.vfMat");
        CHECK(out.params.roadCollider);

        REQUIRE(out.params.road.columns.size() == 4);
        CHECK(out.params.road.columns[0].offset == doctest::Approx(-8.0f)); // -(6 + 2)
        CHECK(out.params.road.columns[1].offset == doctest::Approx(-6.0f));
        CHECK(out.params.road.columns[2].offset == doctest::Approx(6.0f));
        CHECK(out.params.road.columns[3].offset == doctest::Approx(8.0f));
        CHECK(out.params.road.columns[3].heightOffset == doctest::Approx(-0.3f));
        CHECK(out.params.road.columns[3].terrainBlend == doctest::Approx(1.0f));
        CHECK(out.params.road.ringSpacing == doctest::Approx(1.5f));
        CHECK(out.params.road.uvTilingV == doctest::Approx(12.0f));
        CHECK(out.params.road.zOffset == doctest::Approx(0.08f));
        CHECK_FALSE(out.params.road.flatCrossSection);
        CHECK(out.params.road.chunkMinTileFraction == doctest::Approx(0.75f));

        CHECK(out.splineId == 42);
        // Regeneration writes a new .vfMesh revision rather than overwriting a resident one, so a
        // reloaded road MUST remember how far the numbering got or it will write over the file it
        // is currently rendering.
        CHECK(out.revision == 3);
        CHECK(out.chunkCount == 5);
    }

    TEST_CASE("a scene missing the profile keeps the default cross-section")
    {
        resetRoadTestRoot();

        const fs::path scenePath = roadTestRoot() / "NoProfile.vfScene";
        writeScene(scenePath, json{{"controlPoints", json::array({json::array({0.0f, 0.0f, 0.0f}),
                                                                 json::array({10.0f, 0.0f, 0.0f})})},
                                   {"roadName", "Plain"}});

        scene::SceneGraphSystem loaded;
        REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
        REQUIRE(loaded.GetRoot().hasComponent<components::RoadSplineComponent>());

        const auto& out = loaded.GetRoot().getComponent<components::RoadSplineComponent>();
        CHECK(out.controlPoints.size() == 2);
        CHECK(out.params.roadName == "Plain");
        // A road with no columns generates nothing, so the default profile has to survive.
        CHECK(out.params.road.columns.size() == 4);
        CHECK(out.params.road.ringSpacing == doctest::Approx(1.0f));
    }

    TEST_CASE("a stored profile too small to build geometry is refused, not adopted")
    {
        resetRoadTestRoot();

        const fs::path scenePath = roadTestRoot() / "OneColumn.vfScene";
        writeScene(scenePath,
                   json{{"controlPoints", json::array({json::array({0.0f, 0.0f, 0.0f}),
                                                       json::array({10.0f, 0.0f, 0.0f})})},
                        {"profile", json{{"columns", json::array({json{{"offset", 0.0f},
                                                                       {"u", 0.5f},
                                                                       {"heightOffset", 0.0f},
                                                                       {"terrainBlend", 0.0f}}})}}}});

        scene::SceneGraphSystem loaded;
        REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
        REQUIRE(loaded.GetRoot().hasComponent<components::RoadSplineComponent>());

        // One column cannot form a ribbon; taking it would make the road regenerate to nothing.
        const auto& out = loaded.GetRoot().getComponent<components::RoadSplineComponent>();
        CHECK(out.params.road.columns.size() == 4);
    }
}

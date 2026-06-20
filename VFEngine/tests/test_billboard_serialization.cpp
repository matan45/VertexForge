// CPU-only serialization coverage for the billboard-animation fields (Phase 1).
//   1. Backward compatibility: a .vfScene written before the animation fields
//      existed (a "billboard" component block with only the original keys) must
//      load with the static-billboard defaults (cols=1, rows=1, every
//      rate/scroll/pulse/spin/animStartTime=0, worldMarker=false).
//      deserializeBillboard uses j.value(key, default), so a missing key falls
//      back to the default and older scenes keep rendering.
//   2. Round-trip: a component with non-default animation values must save and
//      reload to the same values. Catches a key-name mismatch between
//      serialize and deserialize.
//
// serialize/deserializeBillboard are PRIVATE; the public seam is
// saveScene / loadSceneInto over a SceneGraphSystem, exactly as
// test_decal_serialization.cpp drives decal serialization. No graphics layer.

#include <doctest.h>

#include <serialization/SceneSerialization.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <components/Components.hpp>
#include <asset/AssetDatabase.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    fs::path billboardTestRoot()
    {
        return fs::temp_directory_path() / "vf_billboard_serialization_tests";
    }

    void resetBillboardTestRoot()
    {
        std::error_code ec;
        fs::remove_all(billboardTestRoot(), ec);
        fs::create_directories(billboardTestRoot(), ec);
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

TEST_SUITE("BillboardSerialization")
{
    TEST_CASE("animation fields save and reload through a scene round-trip")
    {
        resetBillboardTestRoot();

        scene::SceneGraphSystem source;
        auto& billboard = source.GetRoot().addOrReplaceComponent<components::BillboardComponent>();
        billboard.iconType = components::BillboardIconType::Billboard;
        billboard.editorOnly = false; // user billboard
        billboard.flipbookColumns = 4u;
        billboard.flipbookRows = 8u;
        billboard.flipbookFrameRate = 30.0f;
        billboard.scrollU = 0.5f;
        billboard.scrollV = -0.25f;
        billboard.pulseAmplitude = 0.6f;
        billboard.pulseFrequency = 2.0f;
        billboard.spinSpeed = 1.25f;
        billboard.animStartTime = 5.0f;
        billboard.worldMarker = true;

        fs::path scenePath = billboardTestRoot() / "AnimatedBillboard.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        // The billboard component block is present in the saved scene.
        auto sceneJson = readJson(scenePath);
        REQUIRE(sceneJson["root"]["components"].contains("billboard"));

        scene::SceneGraphSystem loaded;
        REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
        REQUIRE(loaded.GetRoot().hasComponent<components::BillboardComponent>());

        const auto& after = loaded.GetRoot().getComponent<components::BillboardComponent>();
        CHECK(after.flipbookColumns == 4u);
        CHECK(after.flipbookRows == 8u);
        CHECK(after.flipbookFrameRate == doctest::Approx(30.0f));
        CHECK(after.scrollU == doctest::Approx(0.5f));
        CHECK(after.scrollV == doctest::Approx(-0.25f));
        CHECK(after.pulseAmplitude == doctest::Approx(0.6f));
        CHECK(after.pulseFrequency == doctest::Approx(2.0f));
        CHECK(after.spinSpeed == doctest::Approx(1.25f));
        CHECK(after.animStartTime == doctest::Approx(5.0f));
        CHECK(after.worldMarker == true);
    }

    TEST_CASE("legacy billboard without animation keys loads with static defaults")
    {
        resetBillboardTestRoot();

        // A pre-animation .vfScene: the billboard block carries only original keys.
        fs::path scenePath = billboardTestRoot() / "LegacyBillboard.vfScene";
        json sceneJson;
        sceneJson["version"] = "1.0";
        sceneJson["root"] = {
            {"name", "Root"},
            {"isActive", true},
            {"components", {
                {"billboard", {
                    {"iconType", "billboard"},
                    {"atlasIndex", 7u},
                    {"sizeMode", "worldSpace"},
                    {"size", json::array({2.0f, 2.0f})},
                    {"colorTint", json::array({1.0f, 1.0f, 1.0f, 1.0f})},
                    {"editorOnly", false},
                    {"selectable", true}
                }}
            }},
            {"children", json::array()}
        };

        std::ofstream file(scenePath);
        REQUIRE(file.is_open());
        file << sceneJson.dump(2);
        file.close();

        scene::SceneGraphSystem loaded;
        REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
        REQUIRE(loaded.GetRoot().hasComponent<components::BillboardComponent>());

        const auto& billboard = loaded.GetRoot().getComponent<components::BillboardComponent>();
        // Missing animation keys -> no-animation defaults.
        CHECK(billboard.flipbookColumns == 1u);
        CHECK(billboard.flipbookRows == 1u);
        CHECK(billboard.flipbookFrameRate == doctest::Approx(0.0f));
        CHECK(billboard.scrollU == doctest::Approx(0.0f));
        CHECK(billboard.scrollV == doctest::Approx(0.0f));
        CHECK(billboard.pulseAmplitude == doctest::Approx(0.0f));
        CHECK(billboard.pulseFrequency == doctest::Approx(0.0f));
        CHECK(billboard.spinSpeed == doctest::Approx(0.0f));
        CHECK(billboard.animStartTime == doctest::Approx(0.0f));
        CHECK(billboard.worldMarker == false);

        // Original fields still load alongside the defaults.
        CHECK(billboard.atlasIndex == 7u);
        CHECK(billboard.sizeMode == components::BillboardSizeMode::WorldSpace);
        CHECK(billboard.editorOnly == false);
    }
}

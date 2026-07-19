// VK-1574: CPU-only serialization coverage for the IBL knob fields
// (intensity / rotationDeg / tint) added to IBLComponent.
//   1. Round-trip: non-default knobs must save and reload to the same values.
//   2. Backward compat: a pre-VK-1574 "ibl" block (only "hdrRef") loads with the
//      neutral defaults (intensity=1, rotationDeg=0, tint=(1,1,1)) so old scenes
//      render identically.
//   3. Clean diffs: default knobs are omitted from the saved JSON.
//
// serialize/deserializeIBL* are PRIVATE; the public seam is saveScene /
// loadSceneInto over a SceneGraphSystem, exactly as test_billboard_serialization.cpp
// drives billboard serialization. No graphics layer.

#include <doctest.h>

#include <serialization/SceneSerialization.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <components/Components.hpp>
#include <asset/AssetDatabase.hpp>
#include <asset/AssetRef.hpp>
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

#include <filesystem>
#include <fstream>

namespace
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    fs::path iblTestRoot()
    {
        return fs::temp_directory_path() / "vf_ibl_serialization_tests";
    }

    void resetIBLTestRoot()
    {
        std::error_code ec;
        fs::remove_all(iblTestRoot(), ec);
        fs::create_directories(iblTestRoot(), ec);
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

TEST_SUITE("IBLSerialization")
{
    TEST_CASE("IBL knobs save and reload through a scene round-trip")
    {
        resetIBLTestRoot();

        scene::SceneGraphSystem source;
        auto& ibl = source.GetRoot().addOrReplaceComponent<components::IBLComponent>();
        ibl.hdrRef = asset::AssetRef::fromPath("envs/studio.vfHdr");
        ibl.intensity = 2.5f;
        ibl.rotationDeg = 137.0f;
        ibl.tint = glm::vec3(0.8f, 0.6f, 0.4f); // non-default so a key mismatch is caught

        fs::path scenePath = iblTestRoot() / "IBLScene.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        auto sceneJson = readJson(scenePath);
        REQUIRE(sceneJson["root"]["components"].contains("ibl"));

        scene::SceneGraphSystem loaded;
        REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
        REQUIRE(loaded.GetRoot().hasComponent<components::IBLComponent>());

        const auto& after = loaded.GetRoot().getComponent<components::IBLComponent>();
        CHECK(after.intensity == doctest::Approx(2.5f));
        CHECK(after.rotationDeg == doctest::Approx(137.0f));
        CHECK(after.tint.x == doctest::Approx(0.8f));
        CHECK(after.tint.y == doctest::Approx(0.6f));
        CHECK(after.tint.z == doctest::Approx(0.4f));
    }

    TEST_CASE("legacy IBL block without knob keys loads with neutral defaults")
    {
        resetIBLTestRoot();

        // A pre-VK-1574 .vfScene: the ibl block carries only "hdrRef" (path form).
        fs::path scenePath = iblTestRoot() / "LegacyIBL.vfScene";
        json sceneJson;
        sceneJson["version"] = "1.0";
        sceneJson["root"] = {
            {"name", "Root"},
            {"isActive", true},
            {"components", {
                {"ibl", {
                    {"hdrRef", "envs/legacy.vfHdr"} // path form -> fromPath -> valid -> component added
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
        REQUIRE(loaded.GetRoot().hasComponent<components::IBLComponent>());

        const auto& ibl = loaded.GetRoot().getComponent<components::IBLComponent>();
        // Missing knob keys -> neutral defaults (byte-identical ambient to legacy).
        CHECK(ibl.intensity == doctest::Approx(1.0f));
        CHECK(ibl.rotationDeg == doctest::Approx(0.0f));
        CHECK(ibl.tint.x == doctest::Approx(1.0f));
        CHECK(ibl.tint.y == doctest::Approx(1.0f));
        CHECK(ibl.tint.z == doctest::Approx(1.0f));
    }

    TEST_CASE("default IBL knobs are omitted from the saved scene (clean diffs)")
    {
        resetIBLTestRoot();

        scene::SceneGraphSystem source;
        auto& ibl = source.GetRoot().addOrReplaceComponent<components::IBLComponent>();
        ibl.hdrRef = asset::AssetRef::fromPath("envs/default.vfHdr");
        // knobs left at defaults (1 / 0 / (1,1,1))

        fs::path scenePath = iblTestRoot() / "DefaultIBL.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        auto sceneJson = readJson(scenePath);
        REQUIRE(sceneJson["root"]["components"].contains("ibl"));
        const auto& iblJson = sceneJson["root"]["components"]["ibl"];
        CHECK_FALSE(iblJson.contains("intensity"));
        CHECK_FALSE(iblJson.contains("rotationDeg"));
        CHECK_FALSE(iblJson.contains("tint"));
    }
}

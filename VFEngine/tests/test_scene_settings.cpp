#include <doctest.h>
#include <serialization/SceneSerialization.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <asset/AssetDatabase.hpp>
#include <asset/AssetMetadataSerializer.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    fs::path testRoot()
    {
        return fs::temp_directory_path() / "vf_scene_settings_tests";
    }

    json readJson(const fs::path& path)
    {
        std::ifstream file(path);
        REQUIRE(file.is_open());
        json j;
        file >> j;
        return j;
    }

    void resetTestRoot()
    {
        std::error_code ec;
        fs::remove_all(testRoot(), ec);
        fs::create_directories(testRoot(), ec);
        asset::AssetDatabase::instance().clear();
    }
}

TEST_SUITE("SceneSettingsSerialization")
{
    TEST_CASE("saveScene writes linked vfSettings asset and metadata")
    {
        resetTestRoot();

        scene::SceneGraphSystem sceneGraph;
        auto physics = types::PhysicsSettings::createDefault();
        physics.gravityScale = 2.5f;
        sceneGraph.setPhysicsSettings(physics);

        auto audio = types::AudioSettings::createDefault();
        audio.masterVolume = 0.35f;
        sceneGraph.setAudioSettings(audio);

        auto render = types::RenderSettings::createDefault();
        render.shadows.enabled = false;
        sceneGraph.setRenderSettings(render);
        sceneGraph.setInputMappingPath("config/input.vfInputMapping");

        fs::path scenePath = testRoot() / "TestScene.vfScene";
        CHECK(serialization::SceneSerialization::saveScene(sceneGraph, scenePath.string()));

        fs::path settingsPath = testRoot() / "TestScene.vfSettings";
        fs::path metaPath = asset::AssetMetadataSerializer::getMetaPath(settingsPath);

        CHECK(fs::exists(scenePath));
        CHECK(fs::exists(settingsPath));
        CHECK(fs::exists(metaPath));

        auto sceneJson = readJson(scenePath);
        CHECK(sceneJson.contains("settingsRef"));
        CHECK(sceneJson.contains("settingsRefPath"));
        CHECK(sceneJson["settingsRefPath"].get<std::string>() == "TestScene.vfSettings");
        CHECK_FALSE(sceneJson.contains("physicsSettings"));
        CHECK_FALSE(sceneJson.contains("audioSettings"));
        CHECK_FALSE(sceneJson.contains("renderSettings"));

        auto settingsJson = readJson(settingsPath);
        CHECK(settingsJson["physicsSettings"]["gravityScale"].get<float>() == doctest::Approx(2.5f));
        CHECK(settingsJson["audioSettings"]["listener"]["masterVolume"].get<float>() == doctest::Approx(0.35f));
        CHECK(settingsJson["renderSettings"]["shadows"]["enabled"].get<bool>() == false);
        CHECK(settingsJson["inputMapping"].get<std::string>() == "config/input.vfInputMapping");

        auto meta = asset::AssetMetadataSerializer::load(metaPath);
        REQUIRE(meta.has_value());
        CHECK(meta->guid.toString() == sceneJson["settingsRef"].get<std::string>());
        CHECK(meta->type == resource::AssetType::Scene);
    }

    TEST_CASE("loadScene reads settings through asset ref path fallback")
    {
        resetTestRoot();

        scene::SceneGraphSystem source;
        auto physics = types::PhysicsSettings::createDefault();
        physics.gravityScale = 3.0f;
        source.setPhysicsSettings(physics);

        auto audio = types::AudioSettings::createDefault();
        audio.masterVolume = 0.2f;
        source.setAudioSettings(audio);

        auto render = types::RenderSettings::createDefault();
        render.distanceCulling.enabled = true;
        source.setRenderSettings(render);

        fs::path scenePath = testRoot() / "RoundTrip.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        asset::AssetDatabase::instance().clear();

        scene::SceneGraphSystem loaded;
        CHECK(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
        CHECK(loaded.getPhysicsSettings().gravityScale == doctest::Approx(3.0f));
        CHECK(loaded.getAudioSettings().masterVolume == doctest::Approx(0.2f));
        CHECK(loaded.getRenderSettings().distanceCulling.enabled);
    }

    TEST_CASE("loadScene falls back to defaults when settings asset ref is missing")
    {
        resetTestRoot();

        fs::path scenePath = testRoot() / "MissingSettings.vfScene";
        json sceneJson;
        sceneJson["version"] = "1.0";
        sceneJson["root"] = {
            {"name", "Root"},
            {"isActive", true},
            {"components", json::object()},
            {"children", json::array()}
        };

        std::ofstream file(scenePath);
        REQUIRE(file.is_open());
        file << sceneJson.dump(2);
        file.close();

        scene::SceneGraphSystem sceneGraph;
        CHECK(serialization::SceneSerialization::loadSceneInto(scenePath.string(), sceneGraph));
        CHECK(sceneGraph.getPhysicsSettings().gravityScale ==
              doctest::Approx(types::PhysicsSettings::createDefault().gravityScale));
    }

    TEST_CASE("loadScene reads legacy inline settings when settingsRef is missing")
    {
        resetTestRoot();

        // Legacy pre-settingsRef format: settings stored at the scene JSON root
        scene::SceneGraphSystem source;
        auto physics = types::PhysicsSettings::createDefault();
        physics.gravityScale = 4.0f;
        source.setPhysicsSettings(physics);

        json sceneJson;
        sceneJson["version"] = "1.0";
        sceneJson["physicsSettings"] = json{{"gravityScale", 4.0f}};
        sceneJson["root"] = {
            {"name", "Root"},
            {"isActive", true},
            {"components", json::object()},
            {"children", json::array()}
        };

        fs::path scenePath = testRoot() / "LegacyInlineSettings.vfScene";
        std::ofstream file(scenePath);
        REQUIRE(file.is_open());
        file << sceneJson.dump(2);
        file.close();

        scene::SceneGraphSystem loaded;
        CHECK(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
        CHECK(loaded.getPhysicsSettings().gravityScale == doctest::Approx(4.0f));
    }
}

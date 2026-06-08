#include <doctest.h>
#include <serialization/SceneSerialization.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <asset/AssetDatabase.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <map>

// VK-1365: per-scene plugin enable overrides in the scene's .vfSettings
namespace
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    fs::path testRoot()
    {
        return fs::temp_directory_path() / "vf_plugin_scene_settings_tests";
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

TEST_SUITE("PluginSceneSettings")
{
    TEST_CASE("pluginSettings overrides round-trip through .vfSettings")
    {
        resetTestRoot();

        scene::SceneGraphSystem source;
        source.setPluginSettings({{"HexTerrain", false}, {"RTSGameplay", true}});

        fs::path scenePath = testRoot() / "Overrides.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        // Object-of-objects shape: {"Name": {"enabled": bool}}
        auto settingsJson = readJson(testRoot() / "Overrides.vfSettings");
        REQUIRE(settingsJson.contains("pluginSettings"));
        CHECK(settingsJson["pluginSettings"]["HexTerrain"]["enabled"].get<bool>() == false);
        CHECK(settingsJson["pluginSettings"]["RTSGameplay"]["enabled"].get<bool>() == true);

        scene::SceneGraphSystem loaded;
        CHECK(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
        const auto& overrides = loaded.getPluginSettings();
        REQUIRE(overrides.size() == 2);
        CHECK(overrides.at("HexTerrain") == false);
        CHECK(overrides.at("RTSGameplay") == true);
    }

    TEST_CASE("empty overrides omit the pluginSettings key entirely")
    {
        resetTestRoot();

        scene::SceneGraphSystem source;
        fs::path scenePath = testRoot() / "NoOverrides.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        auto settingsJson = readJson(testRoot() / "NoOverrides.vfSettings");
        CHECK_FALSE(settingsJson.contains("pluginSettings"));
    }

    TEST_CASE("scenes without pluginSettings load with no overrides (old-scene compat)")
    {
        resetTestRoot();

        scene::SceneGraphSystem source;
        fs::path scenePath = testRoot() / "OldScene.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        // Loading must also CLEAR stale overrides left over from a previous scene
        scene::SceneGraphSystem loaded;
        loaded.setPluginSettings({{"HexTerrain", false}});
        CHECK(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
        CHECK(loaded.getPluginSettings().empty());
    }

    TEST_CASE("malformed pluginSettings entries are skipped")
    {
        resetTestRoot();

        // Hand-author a settings file with one valid and two malformed entries,
        // referenced from a minimal legacy-style scene (inline settings path).
        json sceneJson;
        sceneJson["version"] = "1.0";
        sceneJson["pluginSettings"] = {
            {"Good", {{"enabled", false}}},
            {"NotAnObject", true},
            {"WrongType", {{"enabled", "yes"}}}
        };
        sceneJson["root"] = {
            {"name", "Root"},
            {"isActive", true},
            {"components", json::object()},
            {"children", json::array()}
        };

        fs::path scenePath = testRoot() / "Malformed.vfScene";
        std::ofstream file(scenePath);
        REQUIRE(file.is_open());
        file << sceneJson.dump(2);
        file.close();

        scene::SceneGraphSystem loaded;
        CHECK(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
        const auto& overrides = loaded.getPluginSettings();
        REQUIRE(overrides.size() == 1);
        CHECK(overrides.at("Good") == false);
    }
}

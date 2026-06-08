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

    fs::path decalTestRoot()
    {
        return fs::temp_directory_path() / "vf_decal_serialization_tests";
    }

    void resetDecalTestRoot()
    {
        std::error_code ec;
        fs::remove_all(decalTestRoot(), ec);
        fs::create_directories(decalTestRoot(), ec);
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

TEST_SUITE("DecalSerialization")
{
    TEST_CASE("decal shape saves and loads")
    {
        resetDecalTestRoot();

        scene::SceneGraphSystem source;
        auto& decal = source.GetRoot().addOrReplaceComponent<components::DecalComponent>();
        decal.shape = components::DecalShape::Circle;
        decal.halfExtents = glm::vec3(2.0f, 3.0f, 0.5f);

        fs::path scenePath = decalTestRoot() / "CircleDecal.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        auto sceneJson = readJson(scenePath);
        REQUIRE(sceneJson["root"]["components"].contains("decal"));
        CHECK(sceneJson["root"]["components"]["decal"]["shape"].get<std::string>() == "Circle");

        scene::SceneGraphSystem loaded;
        REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
        REQUIRE(loaded.GetRoot().hasComponent<components::DecalComponent>());

        const auto& loadedDecal = loaded.GetRoot().getComponent<components::DecalComponent>();
        CHECK(loadedDecal.shape == components::DecalShape::Circle);
        CHECK(loadedDecal.halfExtents.x == doctest::Approx(2.0f));
        CHECK(loadedDecal.halfExtents.y == doctest::Approx(3.0f));
        CHECK(loadedDecal.halfExtents.z == doctest::Approx(0.5f));
    }

    TEST_CASE("legacy decal without shape defaults to rectangle")
    {
        resetDecalTestRoot();

        fs::path scenePath = decalTestRoot() / "LegacyDecal.vfScene";
        json sceneJson;
        sceneJson["version"] = "1.0";
        sceneJson["root"] = {
            {"name", "Root"},
            {"isActive", true},
            {"components", {
                {"decal", {
                    {"halfExtents", json::array({1.0f, 1.0f, 0.25f})},
                    {"color", json::array({1.0f, 1.0f, 1.0f, 1.0f})}
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
        REQUIRE(loaded.GetRoot().hasComponent<components::DecalComponent>());

        const auto& decal = loaded.GetRoot().getComponent<components::DecalComponent>();
        CHECK(decal.shape == components::DecalShape::Rectangle);
        CHECK(decal.halfExtents.z == doctest::Approx(0.25f));
    }

    TEST_CASE("numeric decal shape loads for tooling compatibility")
    {
        resetDecalTestRoot();

        fs::path scenePath = decalTestRoot() / "NumericDecal.vfScene";
        json sceneJson;
        sceneJson["version"] = "1.0";
        sceneJson["root"] = {
            {"name", "Root"},
            {"isActive", true},
            {"components", {
                {"decal", {
                    {"shape", 2},
                    {"halfExtents", json::array({1.0f, 1.0f, 0.25f})}
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
        REQUIRE(loaded.GetRoot().hasComponent<components::DecalComponent>());

        CHECK(loaded.GetRoot().getComponent<components::DecalComponent>().shape ==
              components::DecalShape::Triangle);
    }
}

#include <doctest.h>
#include <serialization/SceneSerialization.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <components/Components.hpp>
#include <asset/AssetDatabase.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <vector>
#include <utility>
#include <string>

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

// ============================================================
// Incremental (frame-budgeted) scene load (VK-1268). The budgeted
// loader must produce the same entity tree, in the same DFS pre-order,
// as the synchronous loadSceneInto.
// ============================================================
namespace
{
    using NodeList = std::vector<std::pair<std::string, uint64_t>>;

    // DFS pre-order walk of a loaded scene: (name, uuid) for every entity under
    // the root, children visited in order.
    void collectPreorder(scene::Entity entity, NodeList& out)
    {
        for (auto& child : entity.getChildren())
        {
            out.push_back({child.getName(), child.getUUID().getValue()});
            collectPreorder(child, out);
        }
    }

    json makeNode(const char* name, uint64_t uuid, json children)
    {
        return json{{"name", name}, {"uuid", uuid}, {"isActive", true},
                    {"components", json::object()}, {"children", std::move(children)}};
    }

    // A small nested tree: A(A1,A2), B, C(C1) -> 6 non-root entities.
    fs::path writeNestedScene()
    {
        std::error_code ec;
        fs::path root = fs::temp_directory_path() / "vf_incremental_load_tests";
        fs::create_directories(root, ec);
        asset::AssetDatabase::instance().clear();

        json sceneJson;
        sceneJson["version"] = "1.0";
        sceneJson["root"] = makeNode("Root", 9000, json::array({
            makeNode("A", 9001, json::array({
                makeNode("A1", 9002, json::array()),
                makeNode("A2", 9003, json::array())
            })),
            makeNode("B", 9004, json::array()),
            makeNode("C", 9005, json::array({
                makeNode("C1", 9006, json::array())
            }))
        }));

        fs::path scenePath = root / "Nested.vfScene";
        std::ofstream file(scenePath);
        REQUIRE(file.is_open());
        file << sceneJson.dump(2);
        file.close();
        return scenePath;
    }
}

TEST_SUITE("IncrementalSceneLoad")
{
    TEST_CASE("budgeted load matches synchronous load tree and order")
    {
        fs::path scenePath = writeNestedScene();

        // Synchronous reference.
        scene::SceneGraphSystem syncGraph;
        REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), syncGraph));
        NodeList syncNodes;
        collectPreorder(syncGraph.GetRoot(), syncNodes);
        REQUIRE(syncNodes.size() == 6);

        // Incremental, one entity per step.
        scene::SceneGraphSystem incGraph;
        serialization::IncrementalLoadState state;
        bool stepping = serialization::SceneSerialization::beginIncrementalLoad(
            scenePath.string(), incGraph, nullptr, state);
        REQUIRE(stepping);

        int steps = 0;
        while (stepping)
        {
            stepping = serialization::SceneSerialization::stepIncrementalLoad(state, 1);
            ++steps;
        }
        REQUIRE(state.finished);
        REQUIRE(state.success);
        // Budget of 1 over 6 entities must genuinely span multiple frames.
        CHECK(steps == 6);

        NodeList incNodes;
        collectPreorder(incGraph.GetRoot(), incNodes);

        REQUIRE(incNodes.size() == syncNodes.size());
        CHECK(incNodes == syncNodes);

        // The expected DFS pre-order.
        REQUIRE(incNodes.size() == 6);
        CHECK(incNodes[0].first == "A");
        CHECK(incNodes[1].first == "A1");
        CHECK(incNodes[2].first == "A2");
        CHECK(incNodes[3].first == "B");
        CHECK(incNodes[4].first == "C");
        CHECK(incNodes[5].first == "C1");
    }

    TEST_CASE("a larger budget completes in fewer steps with the same result")
    {
        fs::path scenePath = writeNestedScene();

        scene::SceneGraphSystem incGraph;
        serialization::IncrementalLoadState state;
        bool stepping = serialization::SceneSerialization::beginIncrementalLoad(
            scenePath.string(), incGraph, nullptr, state);
        REQUIRE(stepping);

        // Budget covering every entity finishes in a single step.
        stepping = serialization::SceneSerialization::stepIncrementalLoad(state, 100);
        CHECK_FALSE(stepping);
        REQUIRE(state.success);

        NodeList nodes;
        collectPreorder(incGraph.GetRoot(), nodes);
        CHECK(nodes.size() == 6);
        CHECK(state.fraction() == doctest::Approx(1.0f));
    }
}

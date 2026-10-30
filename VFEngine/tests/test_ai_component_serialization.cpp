#include <doctest.h>
#include <serialization/SceneSerialization.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <components/Components.hpp>
#include <asset/AssetDatabase.hpp>
#include <behaviortree/BlackboardInitialValue.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <variant>

// ============================================================
// VK-1660: scene round trips for the AI component fields:
// BehaviorTreeComponent::initialValues, NavmeshAgentComponent::baseOffset, NavRouteComponent.
// The per-component serializers are private to SceneSerialization, so everything goes through the
// public saveScene / loadSceneInto path the editor uses.
// ============================================================

namespace
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    fs::path aiSerializationTestRoot()
    {
        return fs::temp_directory_path() / "vf_ai_component_serialization_tests";
    }

    void resetAiSerializationTestRoot()
    {
        std::error_code ec;
        fs::remove_all(aiSerializationTestRoot(), ec);
        fs::create_directories(aiSerializationTestRoot(), ec);
        asset::AssetDatabase::instance().clear();
    }

    template <typename T>
    const T* valueOf(const components::BehaviorTreeComponent& tree, const std::string& key)
    {
        for (const auto& entry : tree.initialValues)
        {
            if (entry.key == key)
                return std::get_if<T>(&entry.value);
        }
        return nullptr;
    }

    // Writes a minimal scene whose root carries `components`, then loads it.
    void loadHandWrittenScene(const std::string& fileName, const json& components, scene::SceneGraphSystem& loaded)
    {
        json sceneJson;
        sceneJson["version"] = "1.0";
        sceneJson["root"] = {
            {"name", "Root"},
            {"isActive", true},
            {"components", components},
            {"children", json::array()}
        };
        const fs::path scenePath = aiSerializationTestRoot() / fileName;
        std::ofstream file(scenePath);
        REQUIRE(file.is_open());
        file << sceneJson.dump(2);
        file.close();
        REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
    }
}

TEST_SUITE("AIComponentSerialization")
{
    TEST_CASE("behavior tree initial values of every type survive a scene round trip")
    {
        resetAiSerializationTestRoot();

        scene::SceneGraphSystem source;
        auto& comp = source.GetRoot().addOrReplaceComponent<components::BehaviorTreeComponent>();
        comp.enabled = false;
        comp.initialValues = {
            {"chaseRange", 15.5f},
            {"hits", int32_t{3}},
            {"angry", true},
            {"label", std::string("guard")},
            {"home", glm::vec3(1.0f, 2.0f, -3.5f)},
            {"target", behaviortree::BlackboardEntityRef{1234567890123ull, "Player"}},
        };

        const fs::path scenePath = aiSerializationTestRoot() / "InitialValues.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        scene::SceneGraphSystem loaded;
        REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
        REQUIRE(loaded.GetRoot().hasComponent<components::BehaviorTreeComponent>());
        const auto& tree = loaded.GetRoot().getComponent<components::BehaviorTreeComponent>();

        CHECK(tree.enabled == false);
        CHECK(tree.isInitialized == false);
        REQUIRE(tree.initialValues.size() == 6);
        CHECK(tree.initialValues == comp.initialValues); // order and every alternative preserved

        REQUIRE(valueOf<float>(tree, "chaseRange") != nullptr);
        CHECK(*valueOf<float>(tree, "chaseRange") == doctest::Approx(15.5f));
        REQUIRE(valueOf<int32_t>(tree, "hits") != nullptr);
        CHECK(*valueOf<int32_t>(tree, "hits") == 3);
        REQUIRE(valueOf<behaviortree::BlackboardEntityRef>(tree, "target") != nullptr);
        CHECK(valueOf<behaviortree::BlackboardEntityRef>(tree, "target")->uuid == 1234567890123ull);
        CHECK(valueOf<behaviortree::BlackboardEntityRef>(tree, "target")->name == "Player");
    }

    TEST_CASE("a behavior tree without initial values writes no initialValues key")
    {
        resetAiSerializationTestRoot();

        scene::SceneGraphSystem source;
        source.GetRoot().addOrReplaceComponent<components::BehaviorTreeComponent>();

        const fs::path scenePath = aiSerializationTestRoot() / "NoInitialValues.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        std::ifstream file(scenePath);
        REQUIRE(file.is_open());
        const json sceneJson = json::parse(file);
        const json& btJson = sceneJson["root"]["components"]["behaviorTree"];
        CHECK(btJson.is_object());
        CHECK_FALSE(btJson.contains("initialValues"));
    }

    TEST_CASE("malformed initial values are dropped and valid ones kept")
    {
        resetAiSerializationTestRoot();

        json components;
        components["behaviorTree"] = {
            {"enabled", true},
            {"initialValues", json::array({
                {{"key", "ok"}, {"type", "Float"}, {"value", 2.0}},
                {{"key", "intAsFloat"}, {"type", "Int"}, {"value", 2.5}},        // not integral
                {{"key", "vecTooShort"}, {"type", "Vec3"}, {"value", {1.0, 2.0}}},
                {{"key", ""}, {"type", "Bool"}, {"value", true}},                // empty key
                {{"key", "unknownType"}, {"type", "Matrix"}, {"value", 1}},
                {{"key", "noValue"}, {"type", "String"}},
                {{"key", "who"}, {"type", "Entity"}, {"value", {{"name", "Player"}}}} // uuid optional
            })}
        };

        scene::SceneGraphSystem loaded;
        loadHandWrittenScene("Malformed.vfScene", components, loaded);
        REQUIRE(loaded.GetRoot().hasComponent<components::BehaviorTreeComponent>());
        const auto& tree = loaded.GetRoot().getComponent<components::BehaviorTreeComponent>();

        REQUIRE(tree.initialValues.size() == 2);
        CHECK(tree.initialValues[0].key == "ok");
        CHECK(tree.initialValues[1].key == "who");
        REQUIRE(std::holds_alternative<behaviortree::BlackboardEntityRef>(tree.initialValues[1].value));
        CHECK(std::get<behaviortree::BlackboardEntityRef>(tree.initialValues[1].value).uuid == 0);
        CHECK(std::get<behaviortree::BlackboardEntityRef>(tree.initialValues[1].value).name == "Player");
    }

    TEST_CASE("navmesh agent baseOffset round-trips and defaults to zero")
    {
        resetAiSerializationTestRoot();

        scene::SceneGraphSystem source;
        auto& agent = source.GetRoot().addOrReplaceComponent<components::NavmeshAgentComponent>();
        agent.baseOffset = 1.25f;
        agent.stoppingDistance = 0.75f;

        const fs::path scenePath = aiSerializationTestRoot() / "BaseOffset.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        scene::SceneGraphSystem loaded;
        REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
        REQUIRE(loaded.GetRoot().hasComponent<components::NavmeshAgentComponent>());
        CHECK(loaded.GetRoot().getComponent<components::NavmeshAgentComponent>().baseOffset == doctest::Approx(1.25f));
        CHECK(loaded.GetRoot().getComponent<components::NavmeshAgentComponent>().stoppingDistance == doctest::Approx(0.75f));

        json components;
        components["navmeshAgent"] = {{"radius", 0.5}};
        scene::SceneGraphSystem legacy;
        loadHandWrittenScene("NoBaseOffset.vfScene", components, legacy);
        REQUIRE(legacy.GetRoot().hasComponent<components::NavmeshAgentComponent>());
        CHECK(legacy.GetRoot().getComponent<components::NavmeshAgentComponent>().baseOffset == doctest::Approx(0.0f));
    }

    TEST_CASE("nav route component round-trips")
    {
        resetAiSerializationTestRoot();

        scene::SceneGraphSystem source;
        auto& route = source.GetRoot().addOrReplaceComponent<components::NavRouteComponent>();
        route.showPath = false;

        const fs::path scenePath = aiSerializationTestRoot() / "NavRoute.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        scene::SceneGraphSystem loaded;
        REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
        REQUIRE(loaded.GetRoot().hasComponent<components::NavRouteComponent>());
        CHECK(loaded.GetRoot().getComponent<components::NavRouteComponent>().showPath == false);
    }
}

#include <doctest.h>
#include <serialization/SceneSerialization.hpp>
#include <serialization/PrefabSerialization.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <scene/Entity.hpp>
#include <components/Components.hpp>
#include <asset/AssetDatabase.hpp>
#include <asset/AssetRef.hpp>
#include <behaviortree/BlackboardInitialValue.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <variant>

// ============================================================
// VK-1660: scene round trips for the AI component fields:
// BehaviorTreeComponent::initialValues, NavmeshAgentComponent::baseOffset, NavRouteComponent, and the
// redesigned VolumetricNavVolume / VolumetricAgent components (scene and prefab).
// The per-component serializers are private to SceneSerialization, so everything goes through the
// public saveScene / loadSceneInto and savePrefab / loadPrefab paths the editor uses.
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

    TEST_CASE("volumetric volume and agent round-trip through a scene and a prefab")
    {
        resetAiSerializationTestRoot();

        // A baked file, so bakedRef is a registered asset.
        const fs::path bakedPath = aiSerializationTestRoot() / "Volume.vfVolNav";
        {
            std::ofstream baked(bakedPath, std::ios::binary);
            REQUIRE(baked.is_open());
            baked << "VNVO";
        }
        const asset::AssetRef bakedRef = asset::AssetRef::fromPath(bakedPath.string());
        REQUIRE(bakedRef.isValid());

        components::VolumetricNavVolumeComponent volumeValues;
        volumeValues.centerOffset = glm::vec3(1.0f, 2.0f, -3.0f);
        volumeValues.halfExtents = glm::vec3(4.0f, 5.5f, 6.0f);
        volumeValues.voxelSize = 0.5f;
        volumeValues.connectivity = 6;
        volumeValues.agentClearance = 0.75f;
        volumeValues.includeTerrain = false;
        volumeValues.includeStaticMeshes = true;
        volumeValues.includeColliders = false;
        volumeValues.bakedRef = bakedRef;
        volumeValues.isBaked = true; // runtime state: never saved

        components::VolumetricAgentComponent agentValues;
        agentValues.radius = 0.6f;
        agentValues.maxSpeed = 7.0f;
        agentValues.maxAcceleration = 12.0f;
        agentValues.stoppingDistance = 0.4f;
        agentValues.turnSpeed = 180.0f;
        agentValues.alignPitch = true;
        agentValues.isActive = true; // runtime state: never saved

        auto checkVolume = [&](const components::VolumetricNavVolumeComponent& volume)
        {
            CHECK(volume.centerOffset == volumeValues.centerOffset);
            CHECK(volume.halfExtents == volumeValues.halfExtents);
            CHECK(volume.voxelSize == doctest::Approx(0.5f));
            CHECK(volume.connectivity == 6);
            CHECK(volume.agentClearance == doctest::Approx(0.75f));
            CHECK(volume.includeTerrain == false);
            CHECK(volume.includeStaticMeshes == true);
            CHECK(volume.includeColliders == false);
            CHECK(volume.bakedRef == bakedRef);
            CHECK(volume.isBaked == false);
        };
        auto checkAgent = [&](const components::VolumetricAgentComponent& agent)
        {
            CHECK(agent.radius == doctest::Approx(0.6f));
            CHECK(agent.maxSpeed == doctest::Approx(7.0f));
            CHECK(agent.maxAcceleration == doctest::Approx(12.0f));
            CHECK(agent.stoppingDistance == doctest::Approx(0.4f));
            CHECK(agent.turnSpeed == doctest::Approx(180.0f));
            CHECK(agent.alignPitch == true);
            CHECK(agent.isActive == false);
        };

        SUBCASE("scene")
        {
            scene::SceneGraphSystem source;
            source.GetRoot().addOrReplaceComponent<components::VolumetricNavVolumeComponent>() = volumeValues;
            source.GetRoot().addOrReplaceComponent<components::VolumetricAgentComponent>() = agentValues;

            const fs::path scenePath = aiSerializationTestRoot() / "Volumetric.vfScene";
            REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

            {
                std::ifstream file(scenePath);
                REQUIRE(file.is_open());
                const json sceneJson = json::parse(file);
                const json& volumeJson = sceneJson["root"]["components"]["volumetricNavVolume"];
                CHECK(volumeJson.contains("bakedRef")); // ends in "Ref": the dependency scanner follows it
                CHECK_FALSE(volumeJson.contains("boundsMin"));
            }

            scene::SceneGraphSystem loaded;
            REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
            REQUIRE(loaded.GetRoot().hasComponent<components::VolumetricNavVolumeComponent>());
            REQUIRE(loaded.GetRoot().hasComponent<components::VolumetricAgentComponent>());
            checkVolume(loaded.GetRoot().getComponent<components::VolumetricNavVolumeComponent>());
            checkAgent(loaded.GetRoot().getComponent<components::VolumetricAgentComponent>());
        }

        SUBCASE("prefab")
        {
            scene::SceneGraphSystem source;
            scene::Entity entity("VolumetricPrefabEntity");
            source.addChild(source.GetRoot(), entity);
            entity.addOrReplaceComponent<components::VolumetricNavVolumeComponent>() = volumeValues;
            entity.addOrReplaceComponent<components::VolumetricAgentComponent>() = agentValues;

            const fs::path prefabPath = aiSerializationTestRoot() / "Volumetric.vfPrefab";
            REQUIRE(serialization::PrefabSerialization::savePrefab(entity, prefabPath.string()));

            scene::SceneGraphSystem dest;
            auto root = dest.GetRoot();
            auto loaded = serialization::PrefabSerialization::loadPrefab(prefabPath.string(), root, dest);
            REQUIRE(loaded.has_value());
            REQUIRE(loaded->hasComponent<components::VolumetricNavVolumeComponent>());
            REQUIRE(loaded->hasComponent<components::VolumetricAgentComponent>());
            checkVolume(loaded->getComponent<components::VolumetricNavVolumeComponent>());
            checkAgent(loaded->getComponent<components::VolumetricAgentComponent>());
        }
    }
}

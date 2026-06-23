// Phase 3 (Play/Stop in-memory snapshot): proves that SceneSerialization's
// in-memory snapshot round-trip reproduces the captured scene and undoes any
// edits made after the snapshot — the property the editor relies on to restore
// (including unsaved edits) on Stop without re-reading the .vfScene from disk.
//
// CPU-only: scene::SceneGraphSystem is constructible without a Vulkan device or
// GLFW window (same as test_scene_settings / test_scene_hierarchy_reorder), and
// createSnapshot/restoreFromSnapshot are pure JSON round-trips over the registry.
#include <doctest.h>
#include <serialization/SceneSerialization.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <scene/Entity.hpp>
#include <components/Components.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    using json = nlohmann::json;

    std::vector<std::string> childNames(scene::Entity& parent)
    {
        std::vector<std::string> names;
        for (auto& child : parent.getChildren())
        {
            names.push_back(child.getName());
        }
        return names;
    }

    bool hasChild(scene::Entity& parent, const std::string& name)
    {
        auto names = childNames(parent);
        return std::find(names.begin(), names.end(), name) != names.end();
    }

    scene::Entity findChild(scene::Entity& parent, const std::string& name)
    {
        for (auto& child : parent.getChildren())
        {
            if (child.getName() == name)
            {
                return child;
            }
        }
        FAIL("child not found: ", name);
        return scene::Entity(name);
    }
}

TEST_SUITE("PieSnapshotRestore")
{
    TEST_CASE("snapshot round-trip reproduces entities and transforms")
    {
        scene::SceneGraphSystem sceneGraph;
        auto& root = sceneGraph.GetRoot();

        scene::Entity alpha("Alpha");
        alpha.getComponent<components::TransformComponent>().position = glm::vec3(1.0f, 2.0f, 3.0f);
        sceneGraph.addChild(root, alpha);

        scene::Entity beta("Beta");
        beta.getComponent<components::TransformComponent>().position = glm::vec3(-4.0f, 0.0f, 5.0f);
        sceneGraph.addChild(root, beta);

        REQUIRE(childNames(root) == std::vector<std::string>{"Alpha", "Beta"});

        json snapshot = serialization::SceneSerialization::createSnapshot(sceneGraph);
        REQUIRE(snapshot.is_object());
        REQUIRE(snapshot.contains("root"));

        bool restored = serialization::SceneSerialization::restoreFromSnapshot(snapshot, sceneGraph);
        REQUIRE(restored);

        auto& restoredRoot = sceneGraph.GetRoot();
        CHECK(childNames(restoredRoot) == std::vector<std::string>{"Alpha", "Beta"});

        auto restoredAlpha = findChild(restoredRoot, "Alpha");
        const auto& alphaPos = restoredAlpha.getComponent<components::TransformComponent>().position;
        CHECK(alphaPos.x == doctest::Approx(1.0f));
        CHECK(alphaPos.y == doctest::Approx(2.0f));
        CHECK(alphaPos.z == doctest::Approx(3.0f));

        auto restoredBeta = findChild(restoredRoot, "Beta");
        const auto& betaPos = restoredBeta.getComponent<components::TransformComponent>().position;
        CHECK(betaPos.x == doctest::Approx(-4.0f));
        CHECK(betaPos.z == doctest::Approx(5.0f));

        sceneGraph.clearScene();
    }

    TEST_CASE("restore undoes edits made after the snapshot (Stop preserves pristine state)")
    {
        scene::SceneGraphSystem sceneGraph;
        auto& root = sceneGraph.GetRoot();

        scene::Entity keep("Keep");
        keep.getComponent<components::TransformComponent>().position = glm::vec3(7.0f, 8.0f, 9.0f);
        sceneGraph.addChild(root, keep);

        scene::Entity doomed("Doomed");
        sceneGraph.addChild(root, doomed);

        // Snapshot the pristine pre-"play" scene.
        json snapshot = serialization::SceneSerialization::createSnapshot(sceneGraph);
        REQUIRE(snapshot.is_object());

        // Simulate play-mode mutations: move Keep, delete Doomed, add a new entity.
        scene::Entity liveKeep = findChild(root, "Keep");
        liveKeep.getComponent<components::TransformComponent>().position = glm::vec3(0.0f);

        scene::Entity liveDoomed = findChild(root, "Doomed");
        sceneGraph.removeEntity(liveDoomed);

        scene::Entity spawned("SpawnedDuringPlay");
        sceneGraph.addChild(root, spawned);

        REQUIRE_FALSE(hasChild(root, "Doomed"));
        REQUIRE(hasChild(root, "SpawnedDuringPlay"));

        // Restore: the mutations must be gone and the pristine scene reproduced.
        bool restored = serialization::SceneSerialization::restoreFromSnapshot(snapshot, sceneGraph);
        REQUIRE(restored);

        auto& restoredRoot = sceneGraph.GetRoot();
        CHECK(hasChild(restoredRoot, "Keep"));
        CHECK(hasChild(restoredRoot, "Doomed"));            // deletion undone
        CHECK_FALSE(hasChild(restoredRoot, "SpawnedDuringPlay")); // play-spawn dropped

        auto restoredKeep = findChild(restoredRoot, "Keep");
        const auto& keepPos = restoredKeep.getComponent<components::TransformComponent>().position;
        CHECK(keepPos.x == doctest::Approx(7.0f));  // moved-during-play change reverted
        CHECK(keepPos.y == doctest::Approx(8.0f));
        CHECK(keepPos.z == doctest::Approx(9.0f));

        sceneGraph.clearScene();
    }

    TEST_CASE("snapshot captures and restores scene settings")
    {
        scene::SceneGraphSystem sceneGraph;

        auto physics = types::PhysicsSettings::createDefault();
        physics.gravityScale = 2.75f;
        sceneGraph.setPhysicsSettings(physics);

        json snapshot = serialization::SceneSerialization::createSnapshot(sceneGraph);
        REQUIRE(snapshot.is_object());

        // Mutate the live settings, then restore.
        auto mutated = types::PhysicsSettings::createDefault();
        mutated.gravityScale = 9.99f;
        sceneGraph.setPhysicsSettings(mutated);
        REQUIRE(sceneGraph.getPhysicsSettings().gravityScale == doctest::Approx(9.99f));

        bool restored = serialization::SceneSerialization::restoreFromSnapshot(snapshot, sceneGraph);
        REQUIRE(restored);
        CHECK(sceneGraph.getPhysicsSettings().gravityScale == doctest::Approx(2.75f));

        sceneGraph.clearScene();
    }
}

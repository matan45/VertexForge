#include <doctest.h>
#include <components/Components.hpp>
#include <components/ComponentClone.hpp>
#include <components/PhysicsAnimationComponent.hpp>
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <serialization/SceneSerialization.hpp>
#include <resource/AssetLifecycleHelpers.hpp>
#include <resource/AssetLifecycleManager.hpp>
#include <resource/AssetTypes.hpp>
#include <asset/AssetExtensions.hpp>
#include <asset/AssetRef.hpp>
#include <asset/AssetGUID.hpp>
#include <asset/AssetDatabase.hpp>
#include <types/PhysicsAnimationTypes.hpp>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <filesystem>

// ============================================================
// VK-1437: PhysicsAnimationComponent in the prefab workflow
//
// CPU-only coverage for the Jolt-free surface: duplicate-time transient reset,
// parity with the serialization reset, the .vfPhysAnim -> AssetType mapping the
// asset lifecycle relies on, and the acquire/release balance. The Play-mode
// spawn/cleanup paths are Jolt-dependent and are verified manually (the Tests
// project does not link Core/Physics/jolt).
// ============================================================

namespace
{
    // Populate a component with non-default authored config AND dirty transient state, so a reset
    // is observable on every field.
    components::PhysicsAnimationComponent makeDirtyPhysAnim()
    {
        components::PhysicsAnimationComponent pa;

        // Authored config (must persist through clone / serialize).
        pa.physicsAnimationRef = asset::AssetRef::fromGUID(asset::AssetGUID::generate());
        pa.config.defaultMode = types::PhysicsAnimationMode::Ragdoll;
        pa.config.defaultMotorStrength = 0.7f;
        types::BoneBodyMapping hips;
        hips.boneName = "Hips";
        hips.mass = 6.0f;
        pa.config.boneBodyMappings.push_back(hips);

        // Transient runtime state (must reset).
        pa.currentMode = types::PhysicsAnimationMode::PoweredRagdoll;
        pa.isInitialized = true;
        pa.transitionProgress = 0.5f;
        pa.ragdollCollisionGroup = 17;
        pa.globalMotorStrength = 0.25f;
        pa.ragdollSettled = true;
        pa.overrideBoneMatrices = {glm::mat4(2.0f)};
        pa.capturedPoseMatrices = {glm::mat4(3.0f), glm::mat4(4.0f)};
        pa.blendOutProgress = 0.3f;

        return pa;
    }
}

TEST_SUITE("PhysicsAnimationPrefab")
{
    // ---- Duplicate-time transient reset (the full clone path) ----

    TEST_CASE("cloneOptionalComponents resets PhysicsAnimationComponent runtime state, keeps authored config")
    {
        scene::Entity src("PhysAnimSource");
        scene::Entity dst("PhysAnimDest");

        src.addComponent<components::PhysicsAnimationComponent>() = makeDirtyPhysAnim();

        components::cloneOptionalComponents(src, dst);

        REQUIRE(dst.hasComponent<components::PhysicsAnimationComponent>());
        const auto& d = dst.getComponent<components::PhysicsAnimationComponent>();

        // Authored config copied.
        CHECK(d.physicsAnimationRef.isValid());
        CHECK(d.config.defaultMode == types::PhysicsAnimationMode::Ragdoll);
        CHECK(d.config.defaultMotorStrength == doctest::Approx(0.7f));
        REQUIRE(d.config.boneBodyMappings.size() == 1);
        CHECK(d.config.boneBodyMappings[0].boneName == "Hips");
        CHECK(d.config.boneBodyMappings[0].mass == doctest::Approx(6.0f));

        // Transient runtime state reset to defaults (not the source's live state).
        CHECK(d.currentMode == types::PhysicsAnimationMode::Ragdoll); // == config.defaultMode
        CHECK_FALSE(d.isInitialized);
        CHECK(d.transitionProgress == doctest::Approx(0.0f));
        CHECK(d.ragdollCollisionGroup == 0u);
        CHECK(d.globalMotorStrength == doctest::Approx(1.0f));
        CHECK_FALSE(d.ragdollSettled);
        CHECK(d.overrideBoneMatrices.empty());
        CHECK(d.capturedPoseMatrices.empty());
        CHECK(d.blendOutProgress == doctest::Approx(1.0f));

        scene::EntityRegistry::getRegistry().destroy(src.getHandle());
        scene::EntityRegistry::getRegistry().destroy(dst.getHandle());
    }

    // ---- Scene save/load preserves authored config (incl. ref) and resets transient state ----
    // Exercises the real (private) deserialize reset through the public saveScene/loadSceneInto path,
    // so the serialization reset stays in lock-step with the clone reset above.

    TEST_CASE("scene save/load preserves PhysicsAnimationComponent config and resets transient state")
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "vf_physanim_prefab_tests";
        std::error_code ec;
        fs::create_directories(dir, ec);
        asset::AssetDatabase::instance().clear();

        scene::SceneGraphSystem source;
        auto& pa = source.GetRoot().addOrReplaceComponent<components::PhysicsAnimationComponent>();
        pa = makeDirtyPhysAnim();
        const asset::AssetGUID refGuid = pa.physicsAnimationRef.getGUID();

        const fs::path scenePath = dir / "PhysAnim.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        scene::SceneGraphSystem loaded;
        REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
        REQUIRE(loaded.GetRoot().hasComponent<components::PhysicsAnimationComponent>());
        const auto& d = loaded.GetRoot().getComponent<components::PhysicsAnimationComponent>();

        // Authored config preserved (incl. the .vfPhysAnim reference by GUID).
        CHECK(d.physicsAnimationRef.isValid());
        CHECK(d.physicsAnimationRef.getGUID() == refGuid);
        CHECK(d.config.defaultMode == types::PhysicsAnimationMode::Ragdoll);
        CHECK(d.config.defaultMotorStrength == doctest::Approx(0.7f));
        REQUIRE(d.config.boneBodyMappings.size() == 1);
        CHECK(d.config.boneBodyMappings[0].boneName == "Hips");
        CHECK(d.config.boneBodyMappings[0].mass == doctest::Approx(6.0f));

        // Transient runtime state reset to defaults on load.
        CHECK(d.currentMode == types::PhysicsAnimationMode::Ragdoll); // == config.defaultMode
        CHECK_FALSE(d.isInitialized);
        CHECK(d.transitionProgress == doctest::Approx(0.0f));
        CHECK(d.ragdollCollisionGroup == 0u);
        CHECK(d.globalMotorStrength == doctest::Approx(1.0f));
        CHECK_FALSE(d.ragdollSettled);
        CHECK(d.overrideBoneMatrices.empty());
        CHECK(d.capturedPoseMatrices.empty());
        CHECK(d.blendOutProgress == doctest::Approx(1.0f));

        fs::remove_all(dir, ec);
    }

    // ---- The .vfPhysAnim -> AssetType mapping the lifecycle helpers depend on ----

    TEST_CASE("AssetExtensions: .vfPhysAnim maps to PhysicsShape")
    {
        CHECK(asset::extensions::typeForExtension(".vfphysanim") == resource::AssetType::PhysicsShape);
        // case-insensitive
        CHECK(asset::extensions::typeForExtension(".vfPhysAnim") == resource::AssetType::PhysicsShape);
        CHECK(asset::extensions::isAssetExtension(".vfphysanim"));
    }

    // ---- Asset lifecycle acquire/release balances physicsAnimationRef ----

    TEST_CASE("acquire/releaseEntityAssets balances the physicsAnimationRef refcount")
    {
        auto& mgr = resource::AssetLifecycleManager::instance();

        scene::Entity entity("PhysAnimLifecycle");
        auto& pa = entity.addComponent<components::PhysicsAnimationComponent>();
        pa.physicsAnimationRef = asset::AssetRef::fromGUID(asset::AssetGUID::generate());
        const asset::AssetGUID guid = pa.physicsAnimationRef.getGUID();
        REQUIRE(pa.physicsAnimationRef.isValid());

        const uint32_t before = mgr.getAssetEntry(guid).refCount;

        resource::acquireEntityAssets(entity, mgr);
        CHECK(mgr.getAssetEntry(guid).refCount == before + 1);

        resource::releaseEntityAssets(entity, mgr);
        CHECK(mgr.getAssetEntry(guid).refCount == before);

        scene::EntityRegistry::getRegistry().destroy(entity.getHandle());
    }
}

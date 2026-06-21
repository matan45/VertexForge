#include <doctest.h>

#include <serialization/SceneSerialization.hpp>
#include <serialization/PrefabSerialization.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <scene/Entity.hpp>
#include <components/Components.hpp>
#include <asset/AssetRef.hpp>
#include <asset/AssetGUID.hpp>
#include <asset/AssetDatabase.hpp>

#include <filesystem>
#include <optional>
#include <string>

// ============================================================
// VK-1425 (Part D): VFXSequenceComponent must survive a scene AND prefab
// round-trip with every field intact — sequenceRef (GUID), autoPlay/loop/
// socketName, and the full triggers vector (each eventName + sequenceRef +
// socketName). runtimeComboId is transient and must come back 0.
//
// These go through the public saveScene/loadSceneInto and savePrefab/loadPrefab
// entry points, which exercise the same serializeVFXSequence/deserializeVFXSequence
// codec via the dispatch tables (SceneSerializeDispatch.cpp / PrefabSerialization.cpp).
// AssetRefs are built from GUIDs and compared by GUID, which round-trips exactly
// through the hex representation independent of any AssetDatabase state.
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    fs::path testRoot()
    {
        return fs::temp_directory_path() / "vf_vfx_sequence_component_tests";
    }

    void resetTestRoot()
    {
        std::error_code ec;
        fs::remove_all(testRoot(), ec);
        fs::create_directories(testRoot(), ec);
        asset::AssetDatabase::instance().clear();
    }

    // Build a fully-populated component with two triggers.
    components::VFXSequenceComponent makeComponent()
    {
        components::VFXSequenceComponent seq;
        seq.sequenceRef = asset::AssetRef::fromGUID(asset::AssetGUID::fromValue(0xCAFEull));
        seq.autoPlay = true;
        seq.loop = true;
        seq.socketName = "RightHand";
        seq.runtimeComboId = 4242; // transient — must reset to 0 on load

        components::VFXSequenceTrigger t0;
        t0.eventName = "footstep";
        t0.sequenceRef = asset::AssetRef::fromGUID(asset::AssetGUID::fromValue(0xBEE1ull));
        t0.socketName = "LeftFoot";
        seq.triggers.push_back(t0);

        components::VFXSequenceTrigger t1;
        t1.eventName = "swing";
        t1.sequenceRef = asset::AssetRef::fromGUID(asset::AssetGUID::fromValue(0xBEE2ull));
        t1.socketName = "Weapon";
        seq.triggers.push_back(t1);

        return seq;
    }

    void assertMatches(const components::VFXSequenceComponent& got,
                       const components::VFXSequenceComponent& want)
    {
        CHECK(got.sequenceRef.getGUID() == want.sequenceRef.getGUID());
        CHECK(got.autoPlay == want.autoPlay);
        CHECK(got.loop == want.loop);
        CHECK(got.socketName == want.socketName);
        CHECK(got.runtimeComboId == 0u); // transient

        REQUIRE(got.triggers.size() == want.triggers.size());
        for (size_t i = 0; i < want.triggers.size(); ++i)
        {
            INFO("trigger " << i);
            CHECK(got.triggers[i].eventName == want.triggers[i].eventName);
            CHECK(got.triggers[i].sequenceRef.getGUID() == want.triggers[i].sequenceRef.getGUID());
            CHECK(got.triggers[i].socketName == want.triggers[i].socketName);
        }
    }
}

TEST_SUITE("VFXSequenceComponentSerialization")
{
    TEST_CASE("scene round-trip preserves every VFXSequenceComponent field")
    {
        resetTestRoot();
        const auto want = makeComponent();

        scene::SceneGraphSystem source;
        source.GetRoot().addOrReplaceComponent<components::VFXSequenceComponent>() = want;

        fs::path scenePath = testRoot() / "VfxSeq.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        scene::SceneGraphSystem loaded;
        REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
        REQUIRE(loaded.GetRoot().hasComponent<components::VFXSequenceComponent>());

        assertMatches(loaded.GetRoot().getComponent<components::VFXSequenceComponent>(), want);
    }

    TEST_CASE("prefab round-trip preserves every VFXSequenceComponent field")
    {
        resetTestRoot();
        const auto want = makeComponent();

        scene::SceneGraphSystem source;
        scene::Entity entity("VfxSeqEntity");
        source.addChild(source.GetRoot(), entity);
        entity.addOrReplaceComponent<components::VFXSequenceComponent>() = want;

        fs::path prefabPath = testRoot() / "VfxSeq.vfPrefab";
        REQUIRE(serialization::PrefabSerialization::savePrefab(entity, prefabPath.string()));

        scene::SceneGraphSystem dest;
        auto root = dest.GetRoot();
        auto loadedOpt = serialization::PrefabSerialization::loadPrefab(prefabPath.string(), root, dest);
        REQUIRE(loadedOpt.has_value());
        REQUIRE(loadedOpt->hasComponent<components::VFXSequenceComponent>());

        assertMatches(loadedOpt->getComponent<components::VFXSequenceComponent>(), want);
    }

    TEST_CASE("component with no triggers round-trips with an empty triggers vector")
    {
        resetTestRoot();

        components::VFXSequenceComponent want;
        want.sequenceRef = asset::AssetRef::fromGUID(asset::AssetGUID::fromValue(0xD00Dull));
        want.autoPlay = false;
        want.loop = false;
        want.socketName = "";
        // triggers intentionally empty

        scene::SceneGraphSystem source;
        source.GetRoot().addOrReplaceComponent<components::VFXSequenceComponent>() = want;

        fs::path scenePath = testRoot() / "VfxSeqEmpty.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        scene::SceneGraphSystem loaded;
        REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
        REQUIRE(loaded.GetRoot().hasComponent<components::VFXSequenceComponent>());

        const auto& got = loaded.GetRoot().getComponent<components::VFXSequenceComponent>();
        CHECK(got.sequenceRef.getGUID() == want.sequenceRef.getGUID());
        CHECK(got.autoPlay == false);
        CHECK(got.loop == false);
        CHECK(got.socketName.empty());
        CHECK(got.triggers.empty());
        CHECK(got.runtimeComboId == 0u);
    }
}

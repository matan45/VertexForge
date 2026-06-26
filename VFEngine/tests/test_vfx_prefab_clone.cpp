#include <doctest.h>
#include <components/Components.hpp>
#include <components/ComponentClone.hpp>
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <asset/AssetRef.hpp>
#include <asset/AssetGUID.hpp>

// ============================================================
// VK-1438: duplicating an entity (cloneOptionalComponents) must carry authored VFX / VFXSequence
// config but reset the transient runtime ids, mirroring the deserialize reset in
// SceneSerializePhysicsAnimation.cpp. The VFXSequence case also proves the component is now part of
// OptionalComponents (before VK-1438 it was missing, so it would not clone at all).
// ============================================================

TEST_SUITE("VFXPrefabClone")
{
    TEST_CASE("cloneOptionalComponents keeps VFXComponent config and resets the runtime ids")
    {
        scene::Entity src("VFXSource");
        scene::Entity dst("VFXDest");

        auto& v = src.addComponent<components::VFXComponent>();
        v.vfxRef = asset::AssetRef::fromGUID(asset::AssetGUID::fromValue(0xA11Cull));
        v.autoPlay = false;
        v.loop = false;
        v.priority = 1;
        v.cameraRelative = true;
        v.runtimeInstanceId = 9988; // transient
        v.isPlaying = true;         // transient

        components::cloneOptionalComponents(src, dst);

        REQUIRE(dst.hasComponent<components::VFXComponent>());
        const auto& d = dst.getComponent<components::VFXComponent>();
        CHECK(d.vfxRef.getGUID() == v.vfxRef.getGUID());
        CHECK(d.autoPlay == false);
        CHECK(d.loop == false);
        CHECK(d.priority == 1);
        CHECK(d.cameraRelative == true);
        CHECK(d.runtimeInstanceId == 0u); // reset
        CHECK_FALSE(d.isPlaying);          // reset

        scene::EntityRegistry::getRegistry().destroy(src.getHandle());
        scene::EntityRegistry::getRegistry().destroy(dst.getHandle());
    }

    TEST_CASE("cloneOptionalComponents clones VFXSequenceComponent and resets runtimeComboId")
    {
        scene::Entity src("SeqSource");
        scene::Entity dst("SeqDest");

        auto& s = src.addComponent<components::VFXSequenceComponent>();
        s.sequenceRef = asset::AssetRef::fromGUID(asset::AssetGUID::fromValue(0xBEEFull));
        s.autoPlay = true;
        s.loop = true;
        s.socketName = "Hand";
        components::VFXSequenceTrigger t;
        t.eventName = "hit";
        t.sequenceRef = asset::AssetRef::fromGUID(asset::AssetGUID::fromValue(0xBEE5ull));
        t.socketName = "Sword";
        s.triggers.push_back(t);
        s.runtimeComboId = 7777; // transient

        components::cloneOptionalComponents(src, dst);

        REQUIRE(dst.hasComponent<components::VFXSequenceComponent>());
        const auto& d = dst.getComponent<components::VFXSequenceComponent>();
        CHECK(d.sequenceRef.getGUID() == s.sequenceRef.getGUID());
        CHECK(d.autoPlay == true);
        CHECK(d.loop == true);
        CHECK(d.socketName == "Hand");
        REQUIRE(d.triggers.size() == 1);
        CHECK(d.triggers[0].eventName == "hit");
        CHECK(d.triggers[0].sequenceRef.getGUID() == t.sequenceRef.getGUID());
        CHECK(d.triggers[0].socketName == "Sword");
        CHECK(d.runtimeComboId == 0u); // reset

        scene::EntityRegistry::getRegistry().destroy(src.getHandle());
        scene::EntityRegistry::getRegistry().destroy(dst.getHandle());
    }
}

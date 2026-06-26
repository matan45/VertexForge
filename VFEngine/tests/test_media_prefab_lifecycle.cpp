#include <doctest.h>
#include <components/Components.hpp>
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <resource/AssetLifecycleHelpers.hpp>
#include <resource/AssetLifecycleManager.hpp>
#include <resource/AssetTypes.hpp>
#include <asset/AssetExtensions.hpp>
#include <asset/AssetRef.hpp>
#include <asset/AssetGUID.hpp>

// ============================================================
// VK-1438: acquire/releaseEntityAssets must balance the Billboard textureRef and the VFXSequence
// sequenceRef + every trigger.sequenceRef so a prefab instantiate/delete cycle does not leak (or
// prematurely free) those assets. Also pins the .vfVFXSequence -> AssetType mapping the helpers rely
// on. Mirrors test_physics_animation_prefab.cpp:152-181.
// ============================================================

TEST_SUITE("MediaPrefabLifecycle")
{
    TEST_CASE("AssetExtensions: .vfVFXSequence maps to VFXSequence")
    {
        CHECK(asset::extensions::typeForExtension(".vfvfxsequence") == resource::AssetType::VFXSequence);
        // case-insensitive
        CHECK(asset::extensions::typeForExtension(".vfVFXSequence") == resource::AssetType::VFXSequence);
        CHECK(asset::extensions::isAssetExtension(".vfvfxsequence"));
    }

    TEST_CASE("acquire/releaseEntityAssets balances the Billboard textureRef refcount")
    {
        auto& mgr = resource::AssetLifecycleManager::instance();

        scene::Entity entity("BillboardLifecycle");
        auto& bb = entity.addComponent<components::BillboardComponent>();
        bb.textureRef = asset::AssetRef::fromGUID(asset::AssetGUID::generate());
        const asset::AssetGUID guid = bb.textureRef.getGUID();
        REQUIRE(bb.textureRef.isValid());

        const uint32_t before = mgr.getAssetEntry(guid).refCount;

        resource::acquireEntityAssets(entity, mgr);
        CHECK(mgr.getAssetEntry(guid).refCount == before + 1);

        resource::releaseEntityAssets(entity, mgr);
        CHECK(mgr.getAssetEntry(guid).refCount == before);

        scene::EntityRegistry::getRegistry().destroy(entity.getHandle());
    }

    TEST_CASE("acquire/releaseEntityAssets balances VFXSequence sequenceRef and every trigger ref")
    {
        auto& mgr = resource::AssetLifecycleManager::instance();

        scene::Entity entity("SeqLifecycle");
        auto& seq = entity.addComponent<components::VFXSequenceComponent>();
        seq.sequenceRef = asset::AssetRef::fromGUID(asset::AssetGUID::generate());

        components::VFXSequenceTrigger t0;
        t0.sequenceRef = asset::AssetRef::fromGUID(asset::AssetGUID::generate());
        seq.triggers.push_back(t0);
        components::VFXSequenceTrigger t1;
        t1.sequenceRef = asset::AssetRef::fromGUID(asset::AssetGUID::generate());
        seq.triggers.push_back(t1);

        const asset::AssetGUID mainGuid = seq.sequenceRef.getGUID();
        const asset::AssetGUID trig0 = seq.triggers[0].sequenceRef.getGUID();
        const asset::AssetGUID trig1 = seq.triggers[1].sequenceRef.getGUID();

        const uint32_t bMain = mgr.getAssetEntry(mainGuid).refCount;
        const uint32_t bT0 = mgr.getAssetEntry(trig0).refCount;
        const uint32_t bT1 = mgr.getAssetEntry(trig1).refCount;

        resource::acquireEntityAssets(entity, mgr);
        CHECK(mgr.getAssetEntry(mainGuid).refCount == bMain + 1);
        CHECK(mgr.getAssetEntry(trig0).refCount == bT0 + 1);
        CHECK(mgr.getAssetEntry(trig1).refCount == bT1 + 1);

        resource::releaseEntityAssets(entity, mgr);
        CHECK(mgr.getAssetEntry(mainGuid).refCount == bMain);
        CHECK(mgr.getAssetEntry(trig0).refCount == bT0);
        CHECK(mgr.getAssetEntry(trig1).refCount == bT1);

        scene::EntityRegistry::getRegistry().destroy(entity.getHandle());
    }
}

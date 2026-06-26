#include <doctest.h>

#include <impl/vfx/VFXSequencePlayModeHandler.hpp>
#include <impl/vfx/VFXPlayModeHandler.hpp>
#include <providers/vfx/IVFXRuntimeProvider.hpp>
#include <events/EventDispatcher.hpp>
#include <events/vfx/VFXSequenceRuntimeEvents.hpp>
#include <events/vfx/VFXRuntimeEvents.hpp>
#include <events/editor/EditorModeEvents.hpp>
#include <events/scene/ScenePersistenceEvents.hpp>
#include <events/scene/EntityTransformEvents.hpp>
#include <data/EditorMode.hpp>
#include <data/EntityHandle.hpp>
#include <data/EntityConversion.hpp>
#include <data/VFXTypes.hpp>
#include <data/VFXSequenceTypes.hpp>
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <components/Components.hpp>
#include <asset/AssetRef.hpp>
#include <asset/AssetGUID.hpp>
#include <asset/AssetDatabase.hpp>

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <algorithm>
#include <set>
#include <vector>

// ============================================================
// VK-1438 (AC #5): a prefab instantiated DURING Play must autoplay its VFX / VFXSequence components,
// and deleting it mid-Play must tear the runtime instances/combos down. Both handlers DEFER creation
// to update() (PrefabInstantiatedNotification fires before world transforms settle), so the test
// publishes the notification, asserts nothing spawns yet, then update()s and asserts the spawn.
//
// The runtime create/play/destroy go through the EventDispatcher, so we mock those commands (as
// test_vfx_sequence_forwarding.cpp does). Counts are taken relative to a post-enter-Play baseline so
// the shared global registry / dispatcher state from other suites cannot perturb the assertions.
// ============================================================

namespace
{
    using EditorMode = services::EditorMode;

    void enterPlay()
    {
        ::events::editor::EditorModeChangedNotification n;
        n.previousMode = EditorMode::Edit;
        n.currentMode = EditorMode::Play;
        ::events::EventDispatcher::instance().publish(n);
    }

    // Minimal no-op IVFXRuntimeProvider: the VFX handler only consults isInitialized(); every
    // create/play/destroy is dispatched as a command (mocked below), never called on the provider.
    struct StubVFXProvider : services::IVFXRuntimeProvider
    {
        void init(vk::Format, vk::Format) override {}
        void cleanUp() override {}
        void recreate(vk::Format, vk::Format) override {}
        bool isInitialized() const override { return true; }
        services::VFXInstanceId createInstance(const services::VFXRuntimeParams&) override { return 0; }
        void destroyInstance(services::VFXInstanceId) override {}
        void applyInstanceOverrides(services::VFXInstanceId, const services::VFXEmitterOverrides&) override {}
        void setInstanceTransform(services::VFXInstanceId, const glm::mat4&) override {}
        void playInstance(services::VFXInstanceId) override {}
        void stopInstance(services::VFXInstanceId) override {}
        void resetInstance(services::VFXInstanceId) override {}
        bool isInstancePlaying(services::VFXInstanceId) const override { return false; }
        void update(float) override {}
        void setCamera(const services::VFXCameraParams&) override {}
        void setSceneDepthImageView(vk::ImageView) override {}
        void recordComputeCommands(const vk::CommandBuffer&) override {}
        void recordDrawCommands(const vk::CommandBuffer&) override {}
        bool hasDistortionEmitters() const override { return false; }
        void recordDistortionDrawCommands(const vk::CommandBuffer&) override {}
        void initDistortion(vk::Format, vk::Format) override {}
        void recreateDistortion(vk::Format, vk::Format) override {}
        size_t getInstanceCount() const override { return 0; }
        std::optional<PlaybackState> capturePlaybackState(services::VFXInstanceId) const override { return std::nullopt; }
        void seekInstance(services::VFXInstanceId, float, float) override {}
        void setLightingLayouts(vk::DescriptorSetLayout, vk::DescriptorSetLayout, vk::DescriptorSetLayout) override {}
        void updateLightingDescriptorSets(vk::DescriptorSet, vk::DescriptorSet, vk::DescriptorSet) override {}
        void setDistanceCullingEnabled(bool) override {}
        void setMaxDrawDistance(float) override {}
        std::vector<VFXProxyLight> getActiveProxyLights() const override { return {}; }
        BudgetStats getBudgetStats() const override { return {}; }
        LODConfig getLODConfig() const override { return {}; }
        void setLODConfig(const LODConfig&) override {}
    };
}

TEST_SUITE("VFXPrefabPlayMode")
{
    TEST_CASE("VFXSequence: mid-Play prefab autoplays the combo on update(); delete destroys it")
    {
        asset::AssetDatabase::instance().clear();
        auto& dispatcher = ::events::EventDispatcher::instance();

        // Mock the vfxsequence runtime command surface the handler dispatches into.
        std::vector<services::VFXComboInstanceId> creates;
        std::vector<services::VFXComboInstanceId> plays;
        std::vector<services::VFXComboInstanceId> destroys;
        services::VFXComboInstanceId nextId = 500;
        std::set<services::VFXComboInstanceId> live;

        dispatcher.registerCommandHandler<services::events::vfxsequence::CreateVFXComboInstanceCommand>(
            [&](const services::events::vfxsequence::CreateVFXComboInstanceCommand&) -> services::VFXComboInstanceId
            { services::VFXComboInstanceId id = nextId++; creates.push_back(id); live.insert(id); return id; });
        dispatcher.registerCommandHandler<services::events::vfxsequence::PlayVFXComboInstanceCommand>(
            [&](const services::events::vfxsequence::PlayVFXComboInstanceCommand& c) { plays.push_back(c.comboId); });
        dispatcher.registerCommandHandler<services::events::vfxsequence::DestroyVFXComboInstanceCommand>(
            [&](const services::events::vfxsequence::DestroyVFXComboInstanceCommand& c) { destroys.push_back(c.comboId); live.erase(c.comboId); });
        dispatcher.registerCommandHandler<services::events::vfxsequence::UpdateVFXSequenceRuntimeCommand>(
            [&](const services::events::vfxsequence::UpdateVFXSequenceRuntimeCommand&) {});
        dispatcher.registerQueryHandler<services::events::vfxsequence::IsVFXComboInstancePlayingQuery>(
            [&](const services::events::vfxsequence::IsVFXComboInstancePlayingQuery& q) -> bool { return live.count(q.comboId) > 0; });

        services::VFXSequencePlayModeHandler handler;
        handler.subscribeToEvents();

        enterPlay();
        const size_t baseCreates = creates.size(); // tolerate any unrelated leftover entities

        // Instantiate a "prefab" root mid-Play carrying an autoplay sequence.
        scene::Entity entity("SeqPrefabRoot");
        auto& seq = entity.addComponent<components::VFXSequenceComponent>();
        seq.autoPlay = true;
        seq.sequenceRef = asset::AssetRef::fromGUID(asset::AssetGUID::fromValue(0x5EE1ull));
        entity.addComponent<components::WorldTransformComponent>();
        const services::EntityHandle handle = services::internal::toHandle(entity.getHandle());

        {
            ::events::scene::PrefabInstantiatedNotification n;
            n.rootEntity = handle;
            dispatcher.publish(n);
        }

        // Deferred: nothing created until update() drains (transforms not yet settled at notify time).
        CHECK(creates.size() == baseCreates);

        handler.update(0.016f);
        REQUIRE(creates.size() == baseCreates + 1);
        const services::VFXComboInstanceId comboId = creates.back();
        CHECK(std::count(plays.begin(), plays.end(), comboId) == 1);
        CHECK(entity.getComponent<components::VFXSequenceComponent>().runtimeComboId == comboId);

        // Delete mid-Play -> the combo is destroyed.
        {
            ::events::scene::EntityDeletedNotification n;
            n.entity = handle;
            dispatcher.publish(n);
        }
        CHECK(std::count(destroys.begin(), destroys.end(), comboId) == 1);

        handler.unsubscribeFromEvents();
        scene::EntityRegistry::getRegistry().destroy(entity.getHandle());
    }

    TEST_CASE("VFX: mid-Play prefab autoplays the instance on update(); delete destroys it")
    {
        asset::AssetDatabase::instance().clear();
        auto& dispatcher = ::events::EventDispatcher::instance();

        // Mock the vfxruntime command surface (per-instance, distinct from the combo events above).
        std::vector<services::VFXInstanceId> creates;
        std::vector<services::VFXInstanceId> plays;
        std::vector<services::VFXInstanceId> destroys;
        services::VFXInstanceId nextId = 700;

        dispatcher.registerCommandHandler<services::events::vfxruntime::CreateVFXInstanceCommand>(
            [&](const services::events::vfxruntime::CreateVFXInstanceCommand&) -> services::VFXInstanceId
            { services::VFXInstanceId id = nextId++; creates.push_back(id); return id; });
        dispatcher.registerCommandHandler<services::events::vfxruntime::PlayVFXInstanceCommand>(
            [&](const services::events::vfxruntime::PlayVFXInstanceCommand& c) { plays.push_back(c.instanceId); });
        dispatcher.registerCommandHandler<services::events::vfxruntime::DestroyVFXInstanceCommand>(
            [&](const services::events::vfxruntime::DestroyVFXInstanceCommand& c) { destroys.push_back(c.instanceId); });
        dispatcher.registerCommandHandler<services::events::vfxruntime::UpdateVFXRuntimeCommand>(
            [&](const services::events::vfxruntime::UpdateVFXRuntimeCommand&) {});

        StubVFXProvider provider;
        services::VFXPlayModeHandler handler(&provider);
        handler.subscribeToEvents();

        enterPlay();
        const size_t baseCreates = creates.size();

        scene::Entity entity("VFXPrefabRoot");
        auto& vfx = entity.addComponent<components::VFXComponent>();
        vfx.autoPlay = true;
        vfx.vfxRef = asset::AssetRef::fromGUID(asset::AssetGUID::fromValue(0x7EE1ull));
        entity.addComponent<components::WorldTransformComponent>();
        const services::EntityHandle handle = services::internal::toHandle(entity.getHandle());

        {
            ::events::scene::PrefabInstantiatedNotification n;
            n.rootEntity = handle;
            dispatcher.publish(n);
        }

        // Deferred until update().
        CHECK(creates.size() == baseCreates);

        handler.update(0.016f);
        REQUIRE(creates.size() == baseCreates + 1);
        const services::VFXInstanceId instanceId = creates.back();
        CHECK(std::count(plays.begin(), plays.end(), instanceId) == 1);
        CHECK(entity.getComponent<components::VFXComponent>().runtimeInstanceId == instanceId);

        {
            ::events::scene::EntityDeletedNotification n;
            n.entity = handle;
            dispatcher.publish(n);
        }
        CHECK(std::count(destroys.begin(), destroys.end(), instanceId) == 1);

        handler.unsubscribeFromEvents();
        scene::EntityRegistry::getRegistry().destroy(entity.getHandle());
    }
}

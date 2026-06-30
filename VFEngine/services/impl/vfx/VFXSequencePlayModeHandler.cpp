#include "print/Log.hpp"
#include "VFXSequencePlayModeHandler.hpp"
#include "../SceneSubtreeUtils.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../events/animation/AnimationEventEvents.hpp"
#include "../../events/vfx/VFXSequenceRuntimeEvents.hpp"
#include "../../events/scene/ScenePersistenceEvents.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/Entity.hpp"
#include "components/Components.hpp"
#include <glm/glm.hpp>
#include <algorithm>

namespace services
{
    VFXSequencePlayModeHandler::~VFXSequencePlayModeHandler()
    {
        unsubscribeFromEvents();
    }

    void VFXSequencePlayModeHandler::subscribeToEvents()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        editorModeChangedToken = dispatcher.subscribe<::events::editor::EditorModeChangedNotification>(
            [this](const ::events::editor::EditorModeChangedNotification& n)
            {
                onEditorModeChanged(n.previousMode, n.currentMode);
            });

        transformChangedToken = dispatcher.subscribe<::events::scene::TransformChangedNotification>(
            [this](const ::events::scene::TransformChangedNotification& n)
            {
                if (sequenceActive.load())
                    onTransformChanged(n.entity);
            });

        // Published on a worker/Render task — only touch the mutex-guarded queue here.
        animationEventToken = dispatcher.subscribe<::events::animation::AnimationEventFiredNotification>(
            [this](const ::events::animation::AnimationEventFiredNotification& n)
            {
                if (sequenceActive.load())
                    onAnimationEvent(n.entity, n.eventName);
            });

        // VK-1438: a prefab instantiated AFTER Play started gets no combo from the one-shot Play-entry
        // pass. Queue its root under the mutex; drainPendingPrefabRoots() runs it in update() once
        // transforms are settled. Gated on sequenceActive so Edit-mode instantiation is a no-op.
        prefabInstantiatedToken = dispatcher.subscribe<::events::scene::PrefabInstantiatedNotification>(
            [this](const ::events::scene::PrefabInstantiatedNotification& n)
            {
                if (sequenceActive.load())
                    onPrefabInstantiated(n.rootEntity);
            });

        // VK-1438: destroy the auto-played combo when its entity is deleted during Play.
        // EntityDeletedNotification fires per-entity for the whole subtree before removal.
        entityDeletedToken = dispatcher.subscribe<::events::scene::EntityDeletedNotification>(
            [this](const ::events::scene::EntityDeletedNotification& n)
            {
                if (sequenceActive.load())
                    onEntityDeleted(n.entity);
            });

        sceneLoadedToken = dispatcher.subscribe<::events::scene::SceneLoadedNotification>(
            [this](const ::events::scene::SceneLoadedNotification&)
            {
                if (sequenceActive.load())
                {
                    std::lock_guard<std::mutex> lock(pendingMutex);
                    sceneRescanFrames = kSceneLoadRescanFrames;
                }
            });
    }

    void VFXSequencePlayModeHandler::unsubscribeFromEvents()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        if (editorModeChangedToken.isValid()) { dispatcher.unsubscribe(editorModeChangedToken); editorModeChangedToken = {}; }
        if (transformChangedToken.isValid()) { dispatcher.unsubscribe(transformChangedToken); transformChangedToken = {}; }
        if (animationEventToken.isValid()) { dispatcher.unsubscribe(animationEventToken); animationEventToken = {}; }
        if (prefabInstantiatedToken.isValid()) { dispatcher.unsubscribe(prefabInstantiatedToken); prefabInstantiatedToken = {}; }
        if (entityDeletedToken.isValid()) { dispatcher.unsubscribe(entityDeletedToken); entityDeletedToken = {}; }
        if (sceneLoadedToken.isValid()) { dispatcher.unsubscribe(sceneLoadedToken); sceneLoadedToken = {}; }
    }

    void VFXSequencePlayModeHandler::onEditorModeChanged(EditorMode previousMode, EditorMode currentMode)
    {
        if (previousMode == EditorMode::Edit && currentMode == EditorMode::Play)
            enterPlayMode();
        else if (previousMode == EditorMode::Play && currentMode == EditorMode::Edit)
            exitPlayMode();
    }

    void VFXSequencePlayModeHandler::onAnimationEvent(EntityHandle entity, const std::string& eventName)
    {
        std::lock_guard<std::mutex> lock(pendingMutex);
        if (pendingTriggers.size() >= MAX_PENDING_TRIGGERS)
            return; // drop under flood rather than grow unbounded
        pendingTriggers.push_back(PendingTrigger{entity, eventName});
    }

    void VFXSequencePlayModeHandler::onTransformChanged(EntityHandle entity)
    {
        VFXComboInstanceId comboId = 0;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            auto it = autoPlayCombos.find(entity);
            if (it == autoPlayCombos.end())
                return;
            comboId = it->second;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        auto enttEntity = internal::fromHandle(entity);
        if (!registry.valid(enttEntity) || !registry.all_of<components::WorldTransformComponent>(enttEntity))
            return;

        const auto& worldTransform = registry.get<components::WorldTransformComponent>(enttEntity);

        events::vfxsequence::SetVFXComboInstanceTransformCommand cmd;
        cmd.comboId = comboId;
        cmd.worldTransform = worldTransform.worldMatrix;
        ::events::EventDispatcher::instance().execute(cmd);
    }

    void VFXSequencePlayModeHandler::drainPendingTriggers()
    {
        std::vector<PendingTrigger> triggers;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            if (pendingTriggers.empty())
                return;
            triggers.swap(pendingTriggers);
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        auto& dispatcher = ::events::EventDispatcher::instance();

        for (const auto& trigger : triggers)
        {
            auto enttEntity = internal::fromHandle(trigger.entity);
            if (!registry.valid(enttEntity) || !registry.all_of<components::VFXSequenceComponent>(enttEntity))
                continue;

            const auto& comp = registry.get<components::VFXSequenceComponent>(enttEntity);

            glm::mat4 worldMatrix{1.0f};
            if (registry.all_of<components::WorldTransformComponent>(enttEntity))
                worldMatrix = registry.get<components::WorldTransformComponent>(enttEntity).worldMatrix;

            for (const auto& mapping : comp.triggers)
            {
                if (mapping.eventName != trigger.eventName || !mapping.sequenceRef.isValid())
                    continue;

                events::vfxsequence::CreateVFXComboInstanceCommand createCmd;
                createCmd.sequenceAssetPath = mapping.sequenceRef.resolve();
                createCmd.worldTransform = worldMatrix;
                createCmd.entityId = static_cast<uint32_t>(enttEntity);
                createCmd.autoDestroyOnFinish = true;
                const VFXComboInstanceId comboId = dispatcher.execute(createCmd);
                if (comboId == 0)
                    continue;

                events::vfxsequence::PlayVFXComboInstanceCommand playCmd;
                playCmd.comboId = comboId;
                dispatcher.execute(playCmd);

                if (!mapping.socketName.empty())
                {
                    events::vfxsequence::AttachVFXComboInstanceToSocketCommand attachCmd;
                    attachCmd.comboId = comboId;
                    attachCmd.entityHandle = trigger.entity.id;
                    attachCmd.socketName = mapping.socketName;
                    dispatcher.execute(attachCmd);
                }

                {
                    std::lock_guard<std::mutex> lock(pendingMutex);
                    triggeredCombos.push_back(comboId);
                }
            }
        }
    }

    void VFXSequencePlayModeHandler::tryAutoPlayCombo(EntityHandle handle)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity entity = internal::fromHandle(handle);
        if (!registry.valid(entity) ||
            !registry.all_of<components::VFXSequenceComponent, components::WorldTransformComponent>(entity))
            return;

        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            if (autoPlayCombos.count(handle))
                return; // already auto-playing this entity
        }

        const auto& comp = registry.get<components::VFXSequenceComponent>(entity);
        const bool autoPlay = comp.autoPlay;
        const bool loop = comp.loop;
        const std::string sequencePath = comp.sequenceRef.isValid() ? comp.sequenceRef.resolve() : std::string{};
        const std::string socketName = comp.socketName;
        const auto& worldTransform = registry.get<components::WorldTransformComponent>(entity);

        if (!autoPlay || sequencePath.empty())
            return;

        if (registry.all_of<components::NameComponent>(entity))
        {
            const auto& nameComp = registry.get<components::NameComponent>(entity);
            if (!nameComp.isActive)
                return;
        }

        auto& dispatcher = ::events::EventDispatcher::instance();

        events::vfxsequence::CreateVFXComboInstanceCommand createCmd;
        createCmd.sequenceAssetPath = sequencePath;
        createCmd.worldTransform = worldTransform.worldMatrix;
        createCmd.entityId = static_cast<uint32_t>(entity);
        createCmd.autoDestroyOnFinish = !loop; // looping standalone combos persist until exit-play
        const VFXComboInstanceId comboId = dispatcher.execute(createCmd);
        if (comboId == 0)
            return;

        VFXComboInstanceId activeComboId = comboId;
        bool inserted = false;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            auto [it, didInsert] = autoPlayCombos.emplace(handle, comboId);
            if (!didInsert)
                activeComboId = it->second;
            inserted = didInsert;
        }

        if (!inserted)
        {
            events::vfxsequence::DestroyVFXComboInstanceCommand destroyCmd;
            destroyCmd.comboId = comboId;
            dispatcher.execute(destroyCmd);
            return;
        }

        if (registry.valid(entity) && registry.all_of<components::VFXSequenceComponent>(entity))
            registry.get<components::VFXSequenceComponent>(entity).runtimeComboId = activeComboId;

        events::vfxsequence::PlayVFXComboInstanceCommand playCmd;
        playCmd.comboId = activeComboId;
        dispatcher.execute(playCmd);

        if (!socketName.empty())
        {
            events::vfxsequence::AttachVFXComboInstanceToSocketCommand attachCmd;
            attachCmd.comboId = activeComboId;
            attachCmd.entityHandle = handle.id;
            attachCmd.socketName = socketName;
            dispatcher.execute(attachCmd);
        }
    }

    void VFXSequencePlayModeHandler::enterPlayMode()
    {
        scanAndAutoPlayCombos();

        sequenceActive = true;
        size_t comboCount = 0;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            comboCount = autoPlayCombos.size();
        }
        vfLogInfo("VFX sequence play mode started with {} auto-played combos", comboCount);
    }

    void VFXSequencePlayModeHandler::onPrefabInstantiated(EntityHandle root)
    {
        // Defer to update() so the transform pass has settled world matrices for the new subtree.
        std::lock_guard<std::mutex> lock(pendingMutex);
        pendingPrefabRoots.push_back(root);
    }

    void VFXSequencePlayModeHandler::onEntityDeleted(EntityHandle entity)
    {
        VFXComboInstanceId comboId = 0;

        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            auto it = autoPlayCombos.find(entity);
            if (it != autoPlayCombos.end())
            {
                comboId = it->second;
                autoPlayCombos.erase(it);
            }

            pendingPrefabRoots.erase(
                std::remove(pendingPrefabRoots.begin(), pendingPrefabRoots.end(), entity),
                pendingPrefabRoots.end());
        }

        if (comboId != 0)
        {
            events::vfxsequence::DestroyVFXComboInstanceCommand cmd;
            cmd.comboId = comboId;
            ::events::EventDispatcher::instance().execute(cmd);
        }
    }

    void VFXSequencePlayModeHandler::drainPendingPrefabRoots()
    {
        std::vector<EntityHandle> roots;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            if (pendingPrefabRoots.empty())
                return;
            roots.swap(pendingPrefabRoots);
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        for (EntityHandle root : roots)
        {
            entt::entity rootEntity = internal::fromHandle(root);
            if (!registry.valid(rootEntity))
                continue;

            std::vector<EntityHandle> subtree;
            internal::collectSubtreeHandles(scene::Entity(rootEntity), subtree);
            for (EntityHandle handle : subtree)
            {
                entt::entity entity = internal::fromHandle(handle);
                if (registry.valid(entity) && registry.all_of<components::VFXSequenceComponent>(entity))
                    tryAutoPlayCombo(handle);
            }
        }
    }

    void VFXSequencePlayModeHandler::scanAndAutoPlayCombos()
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        auto view = registry.view<components::VFXSequenceComponent, components::WorldTransformComponent>();
        for (auto entity : view)
        {
            tryAutoPlayCombo(internal::toHandle(entity));
        }
    }

    void VFXSequencePlayModeHandler::exitPlayMode()
    {
        sequenceActive = false;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto& dispatcher = ::events::EventDispatcher::instance();

        auto destroy = [&dispatcher](VFXComboInstanceId comboId)
        {
            events::vfxsequence::DestroyVFXComboInstanceCommand cmd;
            cmd.comboId = comboId;
            dispatcher.execute(cmd);
        };

        std::unordered_map<EntityHandle, VFXComboInstanceId, EntityHandle::Hash> autoCombos;
        std::vector<VFXComboInstanceId> triggerCombos;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            autoCombos.swap(autoPlayCombos);
            triggerCombos.swap(triggeredCombos);
            pendingTriggers.clear();
            pendingPrefabRoots.clear();
            sceneRescanFrames = 0;
        }

        for (const auto& [handle, comboId] : autoCombos)
        {
            destroy(comboId);
            auto enttEntity = internal::fromHandle(handle);
            if (registry.valid(enttEntity) && registry.all_of<components::VFXSequenceComponent>(enttEntity))
                registry.get<components::VFXSequenceComponent>(enttEntity).runtimeComboId = 0;
        }
        for (VFXComboInstanceId comboId : triggerCombos)
            destroy(comboId);

        vfLogInfo("VFX sequence play mode stopped");
    }

    void VFXSequencePlayModeHandler::update(float deltaTime)
    {
        if (!sequenceActive.load())
            return;

        bool doRescan = false;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            if (sceneRescanFrames > 0)
            {
                --sceneRescanFrames;
                doRescan = true;
            }
        }
        if (doRescan)
            scanAndAutoPlayCombos();

        drainPendingPrefabRoots();
        drainPendingTriggers();

        auto& dispatcher = ::events::EventDispatcher::instance();
        events::vfxsequence::UpdateVFXSequenceRuntimeCommand cmd;
        cmd.deltaTime = deltaTime;
        dispatcher.execute(cmd);

        std::vector<VFXComboInstanceId> triggerSnapshot;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            triggerSnapshot = triggeredCombos;
        }

        // Drop ids of triggered combos that have finished/auto-destroyed, so the list does
        // not grow across a long play session (they were only tracked for exit-play teardown).
        if (!triggerSnapshot.empty())
        {
            std::vector<VFXComboInstanceId> finished;
            for (VFXComboInstanceId id : triggerSnapshot)
            {
                events::vfxsequence::IsVFXComboInstancePlayingQuery q;
                q.comboId = id;
                if (!dispatcher.query(q))
                    finished.push_back(id);
            }

            if (!finished.empty())
            {
                std::lock_guard<std::mutex> lock(pendingMutex);
                triggeredCombos.erase(
                    std::remove_if(triggeredCombos.begin(), triggeredCombos.end(),
                        [&finished](VFXComboInstanceId id)
                        {
                            return std::find(finished.begin(), finished.end(), id) != finished.end();
                        }),
                    triggeredCombos.end());
            }
        }
    }
}

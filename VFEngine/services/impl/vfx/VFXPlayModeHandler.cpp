#include "print/Log.hpp"
#include "VFXPlayModeHandler.hpp"
#include "../SceneSubtreeUtils.hpp"
#include "../../providers/vfx/IVFXRuntimeProvider.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../events/vfx/VFXRuntimeEvents.hpp"
#include "../../events/vfx/VFXSnapshotEvents.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../events/world/WorldSectorEvents.hpp"
#include "../../events/scene/ScenePersistenceEvents.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../data/EditorMode.hpp"
#include "../../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/Entity.hpp"
#include "components/Components.hpp"
#include <algorithm>

namespace services
{
    VFXPlayModeHandler::VFXPlayModeHandler(IVFXRuntimeProvider* vfxProvider)
        : vfxProvider(vfxProvider)
    {
    }

    VFXPlayModeHandler::~VFXPlayModeHandler()
    {
        unsubscribeFromEvents();
    }

    void VFXPlayModeHandler::subscribeToEvents()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        editorModeChangedToken = dispatcher.subscribe<::events::editor::EditorModeChangedNotification>(
            [this](const ::events::editor::EditorModeChangedNotification& notification)
            {
                onEditorModeChanged(notification.previousMode, notification.currentMode);
            });

        transformChangedToken = dispatcher.subscribe<::events::scene::TransformChangedNotification>(
            [this](const ::events::scene::TransformChangedNotification& notification)
            {
                if (vfxActive.load())
                {
                    onTransformChanged(notification.entity);
                }
            });

        sectorLoadedToken = dispatcher.subscribe<::events::world::SectorLoadedNotification>(
            [this](const ::events::world::SectorLoadedNotification& notification)
            {
                if (vfxActive.load())
                {
                    onSectorLoaded(notification.coord.x, notification.coord.z);
                }
            });

        sectorUnloadedToken = dispatcher.subscribe<::events::world::SectorUnloadedNotification>(
            [this](const ::events::world::SectorUnloadedNotification& notification)
            {
                if (vfxActive.load())
                {
                    onSectorUnloaded(notification.coord.x, notification.coord.z);
                }
            });

        // VK-1438: a prefab instantiated AFTER Play started gets no VFX from the one-shot Play-entry
        // pass. Queue its root; processPendingPrefabCreates() drains it in update() once transforms
        // are settled. Gated on vfxActive so Edit-mode instantiation is a no-op.
        prefabInstantiatedToken = dispatcher.subscribe<::events::scene::PrefabInstantiatedNotification>(
            [this](const ::events::scene::PrefabInstantiatedNotification& notification)
            {
                if (vfxActive.load())
                {
                    onPrefabInstantiated(notification.rootEntity);
                }
            });

        // VK-1438: destroy a runtime VFX instance when its entity is deleted during Play.
        // EntityDeletedNotification fires per-entity for the whole subtree before removal.
        entityDeletedToken = dispatcher.subscribe<::events::scene::EntityDeletedNotification>(
            [this](const ::events::scene::EntityDeletedNotification& notification)
            {
                if (vfxActive.load())
                {
                    onEntityDeleted(notification.entity);
                }
            });

        sceneLoadedToken = dispatcher.subscribe<::events::scene::SceneLoadedNotification>(
            [this](const ::events::scene::SceneLoadedNotification&)
            {
                if (vfxActive.load())
                {
                    std::lock_guard<std::mutex> lock(pendingMutex);
                    sceneRescanFrames = kSceneLoadRescanFrames;
                }
            });
    }

    void VFXPlayModeHandler::unsubscribeFromEvents()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        if (editorModeChangedToken.isValid())
        {
            dispatcher.unsubscribe(editorModeChangedToken);
            editorModeChangedToken = {};
        }

        if (transformChangedToken.isValid())
        {
            dispatcher.unsubscribe(transformChangedToken);
            transformChangedToken = {};
        }

        if (sectorLoadedToken.isValid())
        {
            dispatcher.unsubscribe(sectorLoadedToken);
            sectorLoadedToken = {};
        }

        if (sectorUnloadedToken.isValid())
        {
            dispatcher.unsubscribe(sectorUnloadedToken);
            sectorUnloadedToken = {};
        }

        if (prefabInstantiatedToken.isValid())
        {
            dispatcher.unsubscribe(prefabInstantiatedToken);
            prefabInstantiatedToken = {};
        }

        if (entityDeletedToken.isValid())
        {
            dispatcher.unsubscribe(entityDeletedToken);
            entityDeletedToken = {};
        }

        if (sceneLoadedToken.isValid())
        {
            dispatcher.unsubscribe(sceneLoadedToken);
            sceneLoadedToken = {};
        }
    }

    void VFXPlayModeHandler::onEditorModeChanged(EditorMode previousMode, EditorMode currentMode)
    {
        if (previousMode == EditorMode::Edit && currentMode == EditorMode::Play)
        {
            enterPlayMode();
        }
        else if (previousMode == EditorMode::Play && currentMode == EditorMode::Edit)
        {
            exitPlayMode();
        }
    }

    void VFXPlayModeHandler::onTransformChanged(EntityHandle entity)
    {
        VFXInstanceId instanceId = 0;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            auto it = activeVFXInstances.find(entity);
            if (it == activeVFXInstances.end())
                return;
            instanceId = it->second;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        auto enttEntity = internal::fromHandle(entity);

        if (!registry.valid(enttEntity) || !registry.all_of<components::WorldTransformComponent>(enttEntity))
        {
            return;
        }

        const auto& worldTransform = registry.get<components::WorldTransformComponent>(enttEntity);

        auto& dispatcher = ::events::EventDispatcher::instance();
        events::vfxruntime::SetVFXInstanceTransformCommand cmd;
        cmd.instanceId = instanceId;
        cmd.worldTransform = worldTransform.worldMatrix;
        dispatcher.execute(cmd);
    }

    void VFXPlayModeHandler::enterPlayMode()
    {
        if (!vfxProvider || !vfxProvider->isInitialized())
        {
            vfLogWarning("VFX runtime provider not available, skipping VFX initialization");
            return;
        }

        scanAndCreateAutoplayInstances();

        vfxActive = true;
        size_t instanceCount = 0;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            instanceCount = activeVFXInstances.size();
        }
        vfLogInfo("VFX play mode started with {} instances", instanceCount);
    }

    void VFXPlayModeHandler::exitPlayMode()
    {
        if (!vfxProvider)
        {
            return;
        }

        vfxActive = false;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto& dispatcher = ::events::EventDispatcher::instance();

        std::unordered_map<EntityHandle, VFXInstanceId, EntityHandle::Hash> instances;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            instances.swap(activeVFXInstances);
            pendingPrefabRoots.clear();
            pendingStreamCreates.clear();
            sceneRescanFrames = 0;
        }

        for (const auto& [handle, instanceId] : instances)
        {
            events::vfxruntime::DestroyVFXInstanceCommand destroyCmd;
            destroyCmd.instanceId = instanceId;
            dispatcher.execute(destroyCmd);

            auto enttEntity = internal::fromHandle(handle);
            if (registry.valid(enttEntity) && registry.all_of<components::VFXComponent>(enttEntity))
            {
                auto& vfxComp = registry.get<components::VFXComponent>(enttEntity);
                vfxComp.runtimeInstanceId = 0;
                vfxComp.isPlaying = false;
            }
        }

        vfLogInfo("VFX play mode stopped");
    }

    void VFXPlayModeHandler::onSectorLoaded(int32_t coordX, int32_t coordZ)
    {
        if (!vfxProvider || !vfxProvider->isInitialized())
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::VFXComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            EntityHandle handle = internal::toHandle(entity);

            // Skip entities that already have active VFX instances
            {
                std::lock_guard<std::mutex> lock(pendingMutex);
                if (activeVFXInstances.count(handle))
                    continue;
            }

            auto& vfxComp = view.get<components::VFXComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            if (!vfxComp.vfxRef.isValid())
                continue;

            if (registry.all_of<components::NameComponent>(entity))
            {
                const auto& nameComp = registry.get<components::NameComponent>(entity);
                if (!nameComp.isActive)
                    continue;
            }

            PendingStreamCreate pending;
            pending.entity = handle;
            pending.vfxPath = vfxComp.vfxRef.resolve();
            pending.worldTransform = worldTransform.worldMatrix;
            pending.loop = vfxComp.loop;
            pending.priority = vfxComp.priority;
            pending.cameraRelative = vfxComp.cameraRelative;
            pending.autoPlay = vfxComp.autoPlay;
            {
                std::lock_guard<std::mutex> lock(pendingMutex);
                if (!activeVFXInstances.count(handle))
                    pendingStreamCreates.push_back(std::move(pending));
            }
        }
    }

    void VFXPlayModeHandler::onSectorUnloaded(int32_t coordX, int32_t coordZ)
    {
        if (!vfxProvider)
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto& dispatcher = ::events::EventDispatcher::instance();

        std::vector<VFXInstanceId> toDestroy;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            for (auto it = activeVFXInstances.begin(); it != activeVFXInstances.end();)
            {
                auto enttEntity = internal::fromHandle(it->first);
                if (!registry.valid(enttEntity))
                {
                    toDestroy.push_back(it->second);
                    it = activeVFXInstances.erase(it);
                }
                else
                {
                    ++it;
                }
            }

            pendingStreamCreates.erase(
                std::remove_if(pendingStreamCreates.begin(), pendingStreamCreates.end(),
                    [&registry](const PendingStreamCreate& p) {
                        auto enttEntity = internal::fromHandle(p.entity);
                        return !registry.valid(enttEntity);
                    }),
                pendingStreamCreates.end());
        }

        for (VFXInstanceId instanceId : toDestroy)
        {
            events::vfxruntime::DestroyVFXInstanceCommand destroyCmd;
            destroyCmd.instanceId = instanceId;
            dispatcher.execute(destroyCmd);
        }
    }

    void VFXPlayModeHandler::processPendingStreamCreates()
    {
        std::vector<PendingStreamCreate> pendingBatch;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            if (pendingStreamCreates.empty())
                return;

            uint32_t created = 0;
            while (!pendingStreamCreates.empty() && created < MAX_STREAMING_CREATES_PER_FRAME)
            {
                pendingBatch.push_back(std::move(pendingStreamCreates.back()));
                pendingStreamCreates.pop_back();
                ++created;
            }
        }

        auto& dispatcher = ::events::EventDispatcher::instance();
        auto& registry = scene::EntityRegistry::getRegistry();

        for (auto& pending : pendingBatch)
        {
            auto enttEntity = internal::fromHandle(pending.entity);
            if (!registry.valid(enttEntity))
                continue;

            VFXInstanceId instanceId = createVFXInstanceForEntity(pending.entity,
                                                                  pending.vfxPath,
                                                                  pending.worldTransform,
                                                                  pending.loop,
                                                                  pending.priority,
                                                                  pending.cameraRelative,
                                                                  pending.autoPlay);
            if (instanceId != 0)
            {
                if (registry.all_of<components::VFXComponent>(enttEntity))
                {
                    auto& vfxComp = registry.get<components::VFXComponent>(enttEntity);

                    // Restore VFX playback state from snapshot if available
                    auto* uuidComp = registry.try_get<components::UUIDComponent>(enttEntity);
                    if (uuidComp)
                    {
                        ::events::vfx::snapshot::GetVFXSnapshotQuery snapQuery;
                        snapQuery.entityUUID = uuidComp->id.getValue();
                        auto snapshot = dispatcher.query(snapQuery);
                        if (snapshot.has_value())
                        {
                            ::events::vfx::snapshot::SeekVFXInstanceCommand seekCmd;
                            seekCmd.instanceId = instanceId;
                            seekCmd.emissionTime = snapshot->emissionTime;
                            seekCmd.spawnAccumulator = snapshot->spawnAccumulator;
                            dispatcher.execute(seekCmd);

                            if (!snapshot->wasPlaying)
                            {
                                events::vfxruntime::StopVFXInstanceCommand stopCmd;
                                stopCmd.instanceId = instanceId;
                                dispatcher.execute(stopCmd);
                                vfxComp.isPlaying = false;
                            }
                        }
                    }
                }
            }
        }
    }

    void VFXPlayModeHandler::onPrefabInstantiated(EntityHandle root)
    {
        // Defer to update() so the transform pass has settled world matrices for the new subtree.
        std::lock_guard<std::mutex> lock(pendingMutex);
        pendingPrefabRoots.push_back(root);
    }

    void VFXPlayModeHandler::onEntityDeleted(EntityHandle entity)
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        VFXInstanceId instanceId = 0;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            auto it = activeVFXInstances.find(entity);
            if (it != activeVFXInstances.end())
            {
                instanceId = it->second;
                activeVFXInstances.erase(it);
            }

            // Drop any not-yet-processed work for this entity.
            pendingPrefabRoots.erase(
                std::remove(pendingPrefabRoots.begin(), pendingPrefabRoots.end(), entity),
                pendingPrefabRoots.end());
            pendingStreamCreates.erase(
                std::remove_if(pendingStreamCreates.begin(), pendingStreamCreates.end(),
                    [&entity](const PendingStreamCreate& p) { return p.entity == entity; }),
                pendingStreamCreates.end());
        }

        if (instanceId != 0)
        {
            events::vfxruntime::DestroyVFXInstanceCommand destroyCmd;
            destroyCmd.instanceId = instanceId;
            dispatcher.execute(destroyCmd);
        }
    }

    void VFXPlayModeHandler::processPendingPrefabCreates()
    {
        if (!vfxProvider || !vfxProvider->isInitialized())
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            pendingPrefabRoots.clear();
            return;
        }

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
                if (!registry.valid(entity) ||
                    !registry.all_of<components::VFXComponent, components::WorldTransformComponent>(entity))
                    continue;

                auto& vfxComp = registry.get<components::VFXComponent>(entity);
                if (!vfxComp.vfxRef.isValid())
                    continue;

                if (registry.all_of<components::NameComponent>(entity) &&
                    !registry.get<components::NameComponent>(entity).isActive)
                    continue;

                const auto& worldTransform = registry.get<components::WorldTransformComponent>(entity);

                createVFXInstanceForEntity(handle,
                                           vfxComp.vfxRef.resolve(),
                                           worldTransform.worldMatrix,
                                           vfxComp.loop,
                                           vfxComp.priority,
                                           vfxComp.cameraRelative,
                                           vfxComp.autoPlay);
            }
        }
    }

    void VFXPlayModeHandler::scanAndCreateAutoplayInstances()
    {
        if (!vfxProvider || !vfxProvider->isInitialized())
        {
            return;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::VFXComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            auto& vfxComp = view.get<components::VFXComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            if (!vfxComp.vfxRef.isValid())
            {
                continue;
            }

            if (registry.all_of<components::NameComponent>(entity))
            {
                const auto& nameComp = registry.get<components::NameComponent>(entity);
                if (!nameComp.isActive)
                {
                    continue;
                }
            }

            createVFXInstanceForEntity(internal::toHandle(entity),
                                       vfxComp.vfxRef.resolve(),
                                       worldTransform.worldMatrix,
                                       vfxComp.loop,
                                       vfxComp.priority,
                                       vfxComp.cameraRelative,
                                       vfxComp.autoPlay);
        }
    }

    VFXInstanceId VFXPlayModeHandler::createVFXInstanceForEntity(EntityHandle handle,
                                                                 const std::string& vfxPath,
                                                                 const glm::mat4& worldTransform,
                                                                 bool loop,
                                                                 uint8_t priority,
                                                                 bool cameraRelative,
                                                                 bool autoPlay)
    {
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            auto it = activeVFXInstances.find(handle);
            if (it != activeVFXInstances.end())
                return it->second;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        auto entity = internal::fromHandle(handle);
        if (!registry.valid(entity))
            return 0;

        auto& dispatcher = ::events::EventDispatcher::instance();
        events::vfxruntime::CreateVFXInstanceCommand createCmd;
        createCmd.params.vfxAssetPath = vfxPath;
        createCmd.params.worldTransform = worldTransform;
        createCmd.params.loop = loop;
        createCmd.params.entityId = static_cast<uint32_t>(entity);
        createCmd.params.priority = static_cast<VFXEmitterPriority>(priority);
        createCmd.params.cameraRelative = cameraRelative;

        const VFXInstanceId createdInstanceId = dispatcher.execute(createCmd);
        if (createdInstanceId == 0)
            return 0;

        VFXInstanceId instanceId = createdInstanceId;
        bool inserted = false;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            auto [it, didInsert] = activeVFXInstances.emplace(handle, createdInstanceId);
            if (!didInsert)
            {
                instanceId = it->second;
            }
            inserted = didInsert;
        }

        if (!inserted)
        {
            events::vfxruntime::DestroyVFXInstanceCommand destroyCmd;
            destroyCmd.instanceId = createdInstanceId;
            dispatcher.execute(destroyCmd);
            return instanceId;
        }

        if (registry.valid(entity) && registry.all_of<components::VFXComponent>(entity))
        {
            auto& vfxComp = registry.get<components::VFXComponent>(entity);
            vfxComp.runtimeInstanceId = instanceId;

            if (autoPlay)
            {
                events::vfxruntime::PlayVFXInstanceCommand playCmd;
                playCmd.instanceId = instanceId;
                dispatcher.execute(playCmd);
                vfxComp.isPlaying = true;
            }
        }

        return instanceId;
    }

    void VFXPlayModeHandler::update(float deltaTime)
    {
        if (!vfxActive.load() || !vfxProvider)
        {
            return;
        }

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
        {
            scanAndCreateAutoplayInstances();
        }

        // Process budgeted streaming creates + mid-Play prefab spawns (transforms now settled)
        processPendingStreamCreates();
        processPendingPrefabCreates();

        auto& dispatcher = ::events::EventDispatcher::instance();
        events::vfxruntime::UpdateVFXRuntimeCommand cmd;
        cmd.deltaTime = deltaTime;
        dispatcher.execute(cmd);
    }
}

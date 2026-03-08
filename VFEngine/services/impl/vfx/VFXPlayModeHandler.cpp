#include "print/Log.hpp"
#include "VFXPlayModeHandler.hpp"
#include "../../providers/vfx/IVFXRuntimeProvider.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../events/vfx/VFXRuntimeEvents.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../events/world/WorldSectorEvents.hpp"
#include "../../data/EditorMode.hpp"
#include "../../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"

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
                if (vfxActive)
                {
                    onTransformChanged(notification.entity);
                }
            });

        sectorLoadedToken = dispatcher.subscribe<::events::world::SectorLoadedNotification>(
            [this](const ::events::world::SectorLoadedNotification& notification)
            {
                if (vfxActive)
                {
                    onSectorLoaded(notification.coord.x, notification.coord.z);
                }
            });

        sectorUnloadedToken = dispatcher.subscribe<::events::world::SectorUnloadedNotification>(
            [this](const ::events::world::SectorUnloadedNotification& notification)
            {
                if (vfxActive)
                {
                    onSectorUnloaded(notification.coord.x, notification.coord.z);
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
        auto it = activeVFXInstances.find(entity);
        if (it == activeVFXInstances.end())
        {
            return;
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
        cmd.instanceId = it->second;
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

        auto& registry = scene::EntityRegistry::getRegistry();
        auto& dispatcher = ::events::EventDispatcher::instance();

        auto view = registry.view<components::VFXComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            auto& vfxComp = view.get<components::VFXComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            if (vfxComp.vfxPath.empty())
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

            events::vfxruntime::CreateVFXInstanceCommand createCmd;
            createCmd.params.vfxAssetPath = vfxComp.vfxPath;
            createCmd.params.worldTransform = worldTransform.worldMatrix;
            createCmd.params.loop = vfxComp.loop;
            createCmd.params.entityId = static_cast<uint32_t>(entity);
            createCmd.params.priority = static_cast<VFXEmitterPriority>(vfxComp.priority);
            createCmd.params.cameraRelative = vfxComp.cameraRelative;

            VFXInstanceId instanceId = dispatcher.execute(createCmd);

            if (instanceId != 0)
            {
                EntityHandle handle = internal::toHandle(entity);
                activeVFXInstances[handle] = instanceId;
                vfxComp.runtimeInstanceId = instanceId;

                if (vfxComp.autoPlay)
                {
                    events::vfxruntime::PlayVFXInstanceCommand playCmd;
                    playCmd.instanceId = instanceId;
                    dispatcher.execute(playCmd);
                    vfxComp.isPlaying = true;
                }
            }
        }

        vfxActive = true;
        vfLogInfo("VFX play mode started with {} instances", activeVFXInstances.size());
    }

    void VFXPlayModeHandler::exitPlayMode()
    {
        if (!vfxProvider)
        {
            return;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        auto& dispatcher = ::events::EventDispatcher::instance();

        for (const auto& [handle, instanceId] : activeVFXInstances)
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

        activeVFXInstances.clear();
        vfxActive = false;

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
            if (activeVFXInstances.count(handle))
                continue;

            auto& vfxComp = view.get<components::VFXComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            if (vfxComp.vfxPath.empty())
                continue;

            if (registry.all_of<components::NameComponent>(entity))
            {
                const auto& nameComp = registry.get<components::NameComponent>(entity);
                if (!nameComp.isActive)
                    continue;
            }

            PendingStreamCreate pending;
            pending.entity = handle;
            pending.vfxPath = vfxComp.vfxPath;
            pending.worldTransform = worldTransform.worldMatrix;
            pending.loop = vfxComp.loop;
            pending.priority = vfxComp.priority;
            pending.cameraRelative = vfxComp.cameraRelative;
            pending.autoPlay = vfxComp.autoPlay;
            pendingStreamCreates.push_back(std::move(pending));
        }
    }

    void VFXPlayModeHandler::onSectorUnloaded(int32_t coordX, int32_t coordZ)
    {
        if (!vfxProvider)
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto& dispatcher = ::events::EventDispatcher::instance();

        // Destroy VFX instances for entities that are no longer valid
        std::vector<EntityHandle> toRemove;
        for (const auto& [handle, instanceId] : activeVFXInstances)
        {
            auto enttEntity = internal::fromHandle(handle);
            if (!registry.valid(enttEntity))
            {
                events::vfxruntime::DestroyVFXInstanceCommand destroyCmd;
                destroyCmd.instanceId = instanceId;
                dispatcher.execute(destroyCmd);
                toRemove.push_back(handle);
            }
        }

        for (const auto& handle : toRemove)
        {
            activeVFXInstances.erase(handle);
        }

        // Also remove any pending creates for now-invalid entities
        pendingStreamCreates.erase(
            std::remove_if(pendingStreamCreates.begin(), pendingStreamCreates.end(),
                [&registry](const PendingStreamCreate& p) {
                    auto enttEntity = internal::fromHandle(p.entity);
                    return !registry.valid(enttEntity);
                }),
            pendingStreamCreates.end());
    }

    void VFXPlayModeHandler::processPendingStreamCreates()
    {
        if (pendingStreamCreates.empty())
            return;

        auto& dispatcher = ::events::EventDispatcher::instance();
        auto& registry = scene::EntityRegistry::getRegistry();

        uint32_t created = 0;
        while (!pendingStreamCreates.empty() && created < MAX_STREAMING_CREATES_PER_FRAME)
        {
            auto pending = std::move(pendingStreamCreates.back());
            pendingStreamCreates.pop_back();

            auto enttEntity = internal::fromHandle(pending.entity);
            if (!registry.valid(enttEntity))
                continue;

            if (activeVFXInstances.count(pending.entity))
                continue;

            events::vfxruntime::CreateVFXInstanceCommand createCmd;
            createCmd.params.vfxAssetPath = pending.vfxPath;
            createCmd.params.worldTransform = pending.worldTransform;
            createCmd.params.loop = pending.loop;
            createCmd.params.entityId = static_cast<uint32_t>(enttEntity);
            createCmd.params.priority = static_cast<VFXEmitterPriority>(pending.priority);
            createCmd.params.cameraRelative = pending.cameraRelative;

            VFXInstanceId instanceId = dispatcher.execute(createCmd);
            if (instanceId != 0)
            {
                activeVFXInstances[pending.entity] = instanceId;

                if (registry.all_of<components::VFXComponent>(enttEntity))
                {
                    auto& vfxComp = registry.get<components::VFXComponent>(enttEntity);
                    vfxComp.runtimeInstanceId = instanceId;

                    if (pending.autoPlay)
                    {
                        events::vfxruntime::PlayVFXInstanceCommand playCmd;
                        playCmd.instanceId = instanceId;
                        dispatcher.execute(playCmd);
                        vfxComp.isPlaying = true;
                    }
                }
                created++;
            }
        }
    }

    void VFXPlayModeHandler::update(float deltaTime)
    {
        if (!vfxActive || !vfxProvider)
        {
            return;
        }

        // Process budgeted streaming creates
        processPendingStreamCreates();

        auto& dispatcher = ::events::EventDispatcher::instance();
        events::vfxruntime::UpdateVFXRuntimeCommand cmd;
        cmd.deltaTime = deltaTime;
        dispatcher.execute(cmd);
    }
}

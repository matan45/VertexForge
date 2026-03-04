#include "print/Log.hpp"
#include "VFXPlayModeHandler.hpp"
#include "../../providers/vfx/IVFXRuntimeProvider.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../events/vfx/VFXRuntimeEvents.hpp"
#include "../../events/project/SceneEvents.hpp"
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

    void VFXPlayModeHandler::update(float deltaTime)
    {
        if (!vfxActive || !vfxProvider)
        {
            return;
        }

        auto& dispatcher = ::events::EventDispatcher::instance();
        events::vfxruntime::UpdateVFXRuntimeCommand cmd;
        cmd.deltaTime = deltaTime;
        dispatcher.execute(cmd);
    }
}

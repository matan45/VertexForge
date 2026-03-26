#include "EditorModeServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../events/render/RenderEvents.hpp"
#include "../../events/terrain/TerrainEvents.hpp"
#include "../../events/terrain/OceanEvents.hpp"
#include "../../data/EntityConversion.hpp"
#include "serialization/SceneSerialization.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"

namespace services
{
    EditorModeServiceImpl::EditorModeServiceImpl(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(std::move(sceneGraph))
    {
    }

    void EditorModeServiceImpl::setMode(EditorMode mode)
    {
        if (currentMode == mode)
        {
            return;
        }

        EditorMode previousMode = currentMode;

        if (mode == EditorMode::Play && previousMode == EditorMode::Edit)
        {
            captureSnapshot();
        }

        // Publish notification BEFORE restoring snapshot so scripts can call onDestroy
        if (mode == EditorMode::Edit && previousMode == EditorMode::Play)
        {
            events::editor::EditorModeChangedNotification notification;
            notification.previousMode = previousMode;
            notification.currentMode = mode;
            events::EventDispatcher::instance().publish(notification);

            restoreSnapshot();
            currentMode = mode;
            return;
        }

        currentMode = mode;

        events::editor::EditorModeChangedNotification notification;
        notification.previousMode = previousMode;
        notification.currentMode = currentMode;
        events::EventDispatcher::instance().publish(notification);
    }

    void EditorModeServiceImpl::captureSnapshot()
    {
        if (!sceneGraph)
        {
            return;
        }

        playModeSnapshot = serialization::SceneSerialization::createSnapshot(*sceneGraph);
    }

    void EditorModeServiceImpl::restoreSnapshot()
    {
        if (!playModeSnapshot.has_value() || !sceneGraph)
        {
            return;
        }

        // Block render preparation from accessing registry during scene transition
        scene::EntityRegistry::setSceneTransitioning(true);

        auto& dispatcher = events::EventDispatcher::instance();

        // Clear entity selection
        events::scene::SelectEntityCommand clearSelectionCmd;
        clearSelectionCmd.entity = std::nullopt;
        dispatcher.execute(clearSelectionCmd);

        // Remove current IBL before clearing scene
        events::render::RemoveIBLCommand removeIblCmd;
        dispatcher.execute(removeIblCmd);

        // Remove all cameras from graphics layer before clearing scene
        auto& registry = scene::EntityRegistry::getRegistry();
        auto cameraView = registry.view<components::CameraComponent>();
        for (auto entity : cameraView)
        {
            const auto& camComp = cameraView.get<components::CameraComponent>(entity);
            events::render::RemoveCameraCommand removeCamCmd;
            removeCamCmd.cameraId = camComp.cameraId;
            dispatcher.execute(removeCamCmd);
        }

        // Restore scene from snapshot
        bool restoreSuccess = serialization::SceneSerialization::restoreFromSnapshot(
            *playModeSnapshot, *sceneGraph);

        if (!restoreSuccess)
        {
            // Leave flag true — cleared next frame by getViewportTexture()
            playModeSnapshot.reset();
            return;
        }

        // Re-set IBL if present on root entity
        scene::Entity& root = sceneGraph->GetRoot();
        if (root.hasComponent<components::IBLComponent>())
        {
            const auto& iblComp = root.getComponent<components::IBLComponent>();
            if (iblComp.hdrRef.isValid())
            {
                events::render::SetIBLCommand setIblCmd;
                setIblCmd.hdrPath = iblComp.hdrRef.resolve();
                dispatcher.execute(setIblCmd);
            }
        }

        // Re-map terrain registrations to restored entity IDs
        events::terrain::RemapTerrainEntitiesCommand remapTerrainCmd;
        dispatcher.execute(remapTerrainCmd);

        // Re-map ocean registrations to restored entity IDs
        events::ocean::RemapOceanEntitiesCommand remapOceanCmd;
        dispatcher.execute(remapOceanCmd);

        // Re-trigger mesh loading for all entities with MeshComponent
        auto meshView = registry.view<components::MeshComponent>();
        for (auto entity : meshView)
        {
            const auto& meshComp = meshView.get<components::MeshComponent>(entity);
            if (meshComp.meshRef.isValid())
            {
                events::scene::MeshDataChangedNotification meshNotif;
                meshNotif.entity = internal::toHandle(entity);
                meshNotif.meshPath = meshComp.meshRef.resolve();
                meshNotif.animatorPath = meshComp.animatorRef.resolve();
                dispatcher.publish(meshNotif);
            }
        }

        // Leave sceneTransitioning = true so the rest of this frame skips render prep.
        // EditorRenderServiceImpl::getViewportTexture() will clear it on the next frame.

        playModeSnapshot.reset();
    }

    EditorMode EditorModeServiceImpl::getMode() const
    {
        return currentMode;
    }

    bool EditorModeServiceImpl::isPlayMode() const
    {
        return currentMode == EditorMode::Play;
    }

    bool EditorModeServiceImpl::isEditMode() const
    {
        return currentMode == EditorMode::Edit;
    }

    void EditorModeServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::editor::SetEditorModeCommand>(
            [this](const events::editor::SetEditorModeCommand& cmd)
            {
                setMode(cmd.mode);
            });

        dispatcher.registerQueryHandler<events::editor::GetEditorModeQuery>(
            [this](const events::editor::GetEditorModeQuery&)
            {
                return getMode();
            });

        dispatcher.registerQueryHandler<events::editor::IsPlayModeQuery>(
            [this](const events::editor::IsPlayModeQuery&)
            {
                return isPlayMode();
            });

        dispatcher.registerQueryHandler<events::editor::IsEditModeQuery>(
            [this](const events::editor::IsEditModeQuery&)
            {
                return isEditMode();
            });
    }
}

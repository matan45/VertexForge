#include "EditorModeServiceImpl.hpp"
#include "../events/EventDispatcher.hpp"
#include "../events/EditorModeEvents.hpp"
#include "../events/SceneEvents.hpp"
#include "../events/RenderEvents.hpp"
#include "../events/TerrainEvents.hpp"
#include "../events/WaterEvents.hpp"
#include "../data/EntityConversion.hpp"
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
        if (!serialization::SceneSerialization::restoreFromSnapshot(*playModeSnapshot, *sceneGraph))
        {
            playModeSnapshot.reset();
            return;
        }

        // Re-set IBL if present on root entity
        scene::Entity& root = sceneGraph->GetRoot();
        if (root.hasComponent<components::IBLComponent>())
        {
            const auto& iblComp = root.getComponent<components::IBLComponent>();
            if (!iblComp.fileName.empty())
            {
                events::render::SetIBLCommand setIblCmd;
                setIblCmd.hdrPath = iblComp.fileName;
                dispatcher.execute(setIblCmd);
            }
        }

        // Re-map terrain registrations to restored entity IDs
        events::terrain::RemapTerrainEntitiesCommand remapTerrainCmd;
        dispatcher.execute(remapTerrainCmd);

        // Re-map water registrations to restored entity IDs
        events::water::RemapWaterEntitiesCommand remapWaterCmd;
        dispatcher.execute(remapWaterCmd);

        // Re-trigger mesh loading for all entities with MeshComponent
        auto meshView = registry.view<components::MeshComponent>();
        for (auto entity : meshView)
        {
            const auto& meshComp = meshView.get<components::MeshComponent>(entity);
            if (!meshComp.meshPath.empty())
            {
                events::scene::MeshDataChangedNotification meshNotif;
                meshNotif.entity = internal::toHandle(entity);
                meshNotif.meshPath = meshComp.meshPath;
                meshNotif.animatorPath = meshComp.animatorPath;
                dispatcher.publish(meshNotif);
            }
        }

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

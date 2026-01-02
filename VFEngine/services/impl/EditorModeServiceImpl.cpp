#include "EditorModeServiceImpl.hpp"
#include "../events/EventDispatcher.hpp"
#include "../events/EditorModeEvents.hpp"
#include "../events/SceneEvents.hpp"
#include "../events/RenderEvents.hpp"
#include "../data/EntityConversion.hpp"
#include "../../utilities/serialization/SceneSerialization.hpp"
#include "../../utilities/scene/SceneGraphSystem.hpp"
#include "../../utilities/scene/EntityRegistry.hpp"
#include "../../utilities/components/Components.hpp"

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

        // Capture snapshot when entering play mode
        if (mode == EditorMode::Play && previousMode == EditorMode::Edit)
        {
            captureSnapshot();
        }

        // Restore snapshot when exiting play mode
        if (mode == EditorMode::Edit && previousMode == EditorMode::Play)
        {
            restoreSnapshot();
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

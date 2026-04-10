#include "EditorModeServiceImpl.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../events/project/SceneEvents.hpp"

namespace services
{
    EditorModeServiceImpl::EditorModeServiceImpl(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(std::move(sceneGraph))
    {
    }

    EditorModeServiceImpl::~EditorModeServiceImpl()
    {
        if (sceneLoadedToken.isValid())
        {
            events::EventDispatcher::instance().unsubscribe(sceneLoadedToken);
        }
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
            paused = false;
            savedScenePath = currentScenePath;
        }

        // Publish notification BEFORE restoring scene so scripts can call onDestroy
        if (mode == EditorMode::Edit && previousMode == EditorMode::Play)
        {
            paused = false;

            events::editor::EditorModeChangedNotification notification;
            notification.previousMode = previousMode;
            notification.currentMode = mode;
            events::EventDispatcher::instance().publish(notification);

            restoreScene();
            currentMode = mode;
            return;
        }

        currentMode = mode;

        events::editor::EditorModeChangedNotification notification;
        notification.previousMode = previousMode;
        notification.currentMode = currentMode;
        events::EventDispatcher::instance().publish(notification);
    }

    void EditorModeServiceImpl::restoreScene()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Cancel any deferred scene loads queued during play mode
        events::scene::CancelPendingSceneLoadsCommand cancelCmd;
        dispatcher.execute(cancelCmd);

        // Clear the current scene
        events::scene::NewSceneCommand newCmd;
        dispatcher.execute(newCmd);

        if (!savedScenePath.empty())
        {
            // Reload the saved scene from disk.
            // This goes through performDeferredLoad which fully restores all subsystems
            // (IBL, terrain, meshes, physics/audio/render settings, etc.).
            events::scene::LoadSceneCommand loadCmd;
            loadCmd.filePath = savedScenePath;
            dispatcher.execute(loadCmd);
        }

        savedScenePath.clear();
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

    void EditorModeServiceImpl::setPaused(bool value)
    {
        if (!isPlayMode() || paused == value)
        {
            return;
        }

        paused = value;

        events::editor::EditorPauseChangedNotification notification;
        notification.paused = paused;
        events::EventDispatcher::instance().publish(notification);
    }

    bool EditorModeServiceImpl::isPaused() const
    {
        return paused;
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

        dispatcher.registerCommandHandler<events::editor::SetEditorPausedCommand>(
            [this](const events::editor::SetEditorPausedCommand& cmd)
            {
                setPaused(cmd.paused);
            });

        dispatcher.registerQueryHandler<events::editor::IsEditorPausedQuery>(
            [this](const events::editor::IsEditorPausedQuery&)
            {
                return isPaused();
            });

        // Track the current scene file path so we can restore it on Stop
        sceneLoadedToken = dispatcher.subscribe<events::scene::SceneLoadedNotification>(
            [this](const events::scene::SceneLoadedNotification& n)
            {
                currentScenePath = n.scenePath;
            });
    }
}

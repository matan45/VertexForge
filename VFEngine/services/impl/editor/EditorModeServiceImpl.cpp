#include "EditorModeServiceImpl.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../events/scripting/ScriptingEvents.hpp"
#include "time/Timer.hpp"
#include "print/Log.hpp"

#include <algorithm>

namespace services
{
    EditorModeServiceImpl::EditorModeServiceImpl(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(std::move(sceneGraph))
    {
    }

    EditorModeServiceImpl::~EditorModeServiceImpl()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        if (sceneLoadedToken.isValid())
        {
            dispatcher.unsubscribe(sceneLoadedToken);
        }
        if (sceneClearedToken.isValid())
        {
            dispatcher.unsubscribe(sceneClearedToken);
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
            // Each play session starts at 1x, unpaused (Phase 2).
            timeScale = 1.0f;
            engineTime::Timer::resetGameTime();
            savedScenePath = currentScenePath;

            // Phase 3: capture the pristine, possibly-unsaved scene into an
            // in-memory snapshot so Stop restores exactly this state (preserving
            // unsaved edits). Done BEFORE EditorModeChangedNotification fires so the
            // snapshot is taken before any play-mode systems mutate the scene.
            // savedScenePath remains the disk fallback if capture fails.
            snapshotCaptured = events::EventDispatcher::instance().execute(
                events::scene::CaptureSceneSnapshotCommand{});
        }

        // Publish notification BEFORE restoring scene so scripts can call onDestroy
        if (mode == EditorMode::Edit && previousMode == EditorMode::Play)
        {
            paused = false;
            // Leave gameplay time in a clean default state on Stop (Phase 2).
            timeScale = 1.0f;
            engineTime::Timer::resetGameTime();

            // Phase 4b: remember the current selection by NAME before tearing the
            // play-mode scene down. The deferred async restore invalidates all entity
            // handles, so we re-resolve by name once SceneLoadedNotification fires.
            captureSelectionForReselect();

            events::editor::EditorModePreChangeNotification preChangeNotification;
            preChangeNotification.previousMode = previousMode;
            preChangeNotification.currentMode = mode;
            events::EventDispatcher::instance().publish(preChangeNotification);

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

        // Cancel any deferred scene loads queued during play mode (needed in both
        // the snapshot and disk-fallback paths).
        events::scene::CancelPendingSceneLoadsCommand cancelCmd;
        dispatcher.execute(cancelCmd);

        // Phase 3: restore the pristine pre-play scene from the in-memory snapshot
        // (preserving unsaved edits, faster than a disk reload). The restore is
        // deferred to the persistence service's update() and routes through
        // finishLoad so all subsystems are re-wired identically. RestoreSceneSnapshot
        // returns false if no snapshot was actually held (defensive: covers a
        // snapshotCaptured/pieSnapshot desync), so we fall through to the disk reload
        // rather than leaving the play-mode scene live with no restore.
        bool restored = false;
        if (snapshotCaptured)
        {
            restored = dispatcher.execute(events::scene::RestoreSceneSnapshotCommand{});
        }

        if (!restored)
        {
            // Fallback: clear and reload the saved scene from disk. This goes through
            // performDeferredLoad which fully restores all subsystems (IBL, terrain,
            // meshes, physics/audio/render settings).
            events::scene::NewSceneCommand newCmd;
            dispatcher.execute(newCmd);

            if (!savedScenePath.empty())
            {
                events::scene::LoadSceneCommand loadCmd;
                loadCmd.filePath = savedScenePath;
                dispatcher.execute(loadCmd);
            }
            else
            {
                // No snapshot and no saved path: nothing will load, so
                // SceneLoadedNotification won't fire — drop the pending reselect so it
                // can't leak onto a later, unrelated scene load (Phase 4b).
                pendingReselectNames.clear();
            }
        }

        snapshotCaptured = false;
        savedScenePath.clear();
    }

    void EditorModeServiceImpl::captureSelectionForReselect()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        pendingReselectNames.clear();

        auto selected = dispatcher.query(events::scene::GetSelectedEntitiesQuery{});
        if (selected.empty())
        {
            return;
        }

        pendingReselectNames.reserve(selected.size());
        for (const auto& handle : selected)
        {
            events::scene::GetEntityQuery q;
            q.entity = handle;
            auto data = dispatcher.query(q);
            if (data && !data->name.empty())
            {
                pendingReselectNames.push_back(data->name);
            }
        }
    }

    void EditorModeServiceImpl::reselectPendingEntities()
    {
        if (pendingReselectNames.empty())
        {
            return;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        std::vector<EntityHandle> resolved;
        resolved.reserve(pendingReselectNames.size());
        for (const auto& name : pendingReselectNames)
        {
            events::scene::FindEntitiesByNameQuery q;
            q.name = name;
            auto matches = dispatcher.query(q);
            // Best-effort: names are not guaranteed unique. Only re-select a name
            // that resolves to exactly one entity so we never restore a different
            // (ambiguous) selection than the user had.
            if (matches.size() == 1)
            {
                resolved.push_back(matches.front());
            }
        }

        pendingReselectNames.clear();

        if (!resolved.empty())
        {
            events::scene::SelectEntitiesCommand cmd;
            cmd.entities = std::move(resolved);
            dispatcher.execute(cmd);
        }
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
        // Single writer pushing gameplay-time state into the Timer (Phase 2).
        engineTime::Timer::setGamePaused(paused);

        events::editor::EditorPauseChangedNotification notification;
        notification.paused = paused;
        events::EventDispatcher::instance().publish(notification);
    }

    bool EditorModeServiceImpl::isPaused() const
    {
        return paused;
    }

    void EditorModeServiceImpl::stepFrame()
    {
        // A step only makes sense while paused in play mode; the Timer consumes the
        // request on its next update() and advances one reproducible gameplay frame.
        if (isPlayMode() && paused)
        {
            engineTime::Timer::requestStep();
        }
    }

    void EditorModeServiceImpl::setTimeScale(float scale)
    {
        timeScale = std::clamp(scale, 0.0f, 8.0f);
        engineTime::Timer::setTimeScale(static_cast<double>(timeScale));

        events::editor::TimeScaleChangedNotification notification;
        notification.scale = timeScale;
        events::EventDispatcher::instance().publish(notification);
    }

    float EditorModeServiceImpl::getTimeScale() const
    {
        return timeScale;
    }

    void EditorModeServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::editor::SetEditorModeCommand>(
            [this](const events::editor::SetEditorModeCommand& cmd)
            {
                auto& d = events::EventDispatcher::instance();

                // Phase 4a: centralized Play gate. Entering Play from Edit requires
                // compiled scripts; rather than silently refusing (as the toolbar UI
                // does), build them first and only proceed if the build succeeds. This
                // path is shared by the toolbar Play button and the F5 hotkey.
                if (cmd.mode == EditorMode::Play && currentMode == EditorMode::Edit)
                {
                    bool compiled = d.query(events::scripting::IsScriptsCompiledQuery{});
                    if (!compiled)
                    {
                        // BuildScriptsCommand is synchronous and returns true on a
                        // successful compile. On failure, abort the mode change so we
                        // never enter Play with stale/broken scripts.
                        bool built = d.execute(events::scripting::BuildScriptsCommand{});
                        if (!built)
                        {
                            vfLogError("[EditorMode] Play aborted: script build failed");
                            return;
                        }
                    }
                }

                // VK-1371: stop the debugger before tearing scripts down on Stop so
                // no script is parked at a breakpoint during teardown; start it
                // before entering Play when requested. Stop is idempotent.
                if (cmd.mode == EditorMode::Edit)
                {
                    d.execute(events::scripting::StopScriptDebuggerCommand{});
                }
                else if (cmd.mode == EditorMode::Play && cmd.withDebugger &&
                         currentMode == EditorMode::Edit)
                {
                    d.execute(events::scripting::StartScriptDebuggerCommand{});
                }

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

        dispatcher.registerCommandHandler<events::editor::StepFrameCommand>(
            [this](const events::editor::StepFrameCommand&)
            {
                stepFrame();
            });

        dispatcher.registerCommandHandler<events::editor::SetTimeScaleCommand>(
            [this](const events::editor::SetTimeScaleCommand& cmd)
            {
                setTimeScale(cmd.scale);
            });

        dispatcher.registerQueryHandler<events::editor::GetTimeScaleQuery>(
            [this](const events::editor::GetTimeScaleQuery&)
            {
                return getTimeScale();
            });

        // Track the current scene file path so we can restore it on Stop
        sceneLoadedToken = dispatcher.subscribe<events::scene::SceneLoadedNotification>(
            [this](const events::scene::SceneLoadedNotification& n)
            {
                currentScenePath = n.scenePath;

                // Phase 4b: if a Stop is in flight (pendingReselectNames is only set
                // on Stop), this is the restored scene finishing its async load — now
                // that fresh handles exist, re-resolve and re-apply the prior
                // selection. Gating on the non-empty list keeps this inert for all
                // other (ordinary) scene loads.
                reselectPendingEntities();
            });

        sceneClearedToken = dispatcher.subscribe<events::scene::SceneClearedNotification>(
            [this](const events::scene::SceneClearedNotification&)
            {
                currentScenePath.clear();
            });

        dispatcher.registerQueryHandler<events::scene::GetCurrentScenePathQuery>(
            [this](const events::scene::GetCurrentScenePathQuery&)
            {
                return currentScenePath;
            });
    }
}

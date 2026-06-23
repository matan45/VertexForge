#pragma once
#include "../../interfaces/editor/IEditorModeService.hpp"
#include "../../events/EventDispatcher.hpp"
#include <memory>
#include <string>
#include <vector>

namespace scene
{
    class SceneGraphSystem;
}

namespace services
{
    class EditorModeServiceImpl : public IEditorModeService
    {
    private:
        EditorMode currentMode = EditorMode::Edit;
        bool paused = false;
        float timeScale = 1.0f;
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

        // Play-mode scene restore: remember which scene file was open when Play was pressed
        std::string currentScenePath;
        std::string savedScenePath;
        // Phase 3: true when an in-memory snapshot was captured on Play, so Stop
        // restores from it (preserving unsaved edits) instead of reloading from disk.
        bool snapshotCaptured = false;
        ::events::SubscriptionToken sceneLoadedToken;
        ::events::SubscriptionToken sceneClearedToken;

        // Phase 4b: names of the entities that were selected when Stop was pressed.
        // The Stop scene restore is deferred + async (it routes through the
        // persistence service and finishes when SceneLoadedNotification fires), and
        // the reload invalidates all entity handles — so we remember selection by
        // NAME and re-resolve fresh handles once the restored scene is loaded.
        // Non-empty only between a Stop and its restore completing; a non-empty list
        // is itself the "reselect pending" signal. Best-effort: entity names are not
        // guaranteed unique, so only names resolving to exactly one entity are kept.
        std::vector<std::string> pendingReselectNames;

    public:
        explicit EditorModeServiceImpl(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);
        ~EditorModeServiceImpl() override;

        void registerEventHandlers() override;

        // Mode Control
        void setMode(EditorMode mode) override;
        EditorMode getMode() const override;

        // Convenience Queries
        bool isPlayMode() const override;
        bool isEditMode() const override;

        // Pause Control
        void setPaused(bool paused) override;
        bool isPaused() const override;

        // Time Control (Phase 2)
        void stepFrame() override;
        void setTimeScale(float scale) override;
        float getTimeScale() const override;

    private:
        void restoreScene();
        // Phase 4b: capture the current selection by name (on Stop) and re-apply it
        // once the restored scene has finished its async load (SceneLoadedNotification).
        void captureSelectionForReselect();
        void reselectPendingEntities();
    };
}

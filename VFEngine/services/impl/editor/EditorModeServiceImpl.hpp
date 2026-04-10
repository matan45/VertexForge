#pragma once
#include "../../interfaces/editor/IEditorModeService.hpp"
#include "../../events/EventDispatcher.hpp"
#include <memory>
#include <string>

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
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

        // Play-mode scene restore: remember which scene file was open when Play was pressed
        std::string currentScenePath;
        std::string savedScenePath;
        ::events::SubscriptionToken sceneLoadedToken;

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

    private:
        void restoreScene();
    };
}

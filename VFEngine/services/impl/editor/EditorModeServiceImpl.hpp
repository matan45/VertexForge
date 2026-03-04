#pragma once
#include "../../interfaces/editor/IEditorModeService.hpp"
#include <nlohmann/json.hpp>
#include <optional>
#include <memory>

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
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;
        std::optional<nlohmann::json> playModeSnapshot;

    public:
        explicit EditorModeServiceImpl(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);
        ~EditorModeServiceImpl() override = default;

        void registerEventHandlers() override;

        // Mode Control
        void setMode(EditorMode mode) override;
        EditorMode getMode() const override;

        // Convenience Queries
        bool isPlayMode() const override;
        bool isEditMode() const override;

    private:
        void captureSnapshot();
        void restoreSnapshot();
    };
}

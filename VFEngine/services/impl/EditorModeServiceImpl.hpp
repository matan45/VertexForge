#pragma once
#include "../interfaces/IEditorModeService.hpp"
#include <nlohmann/json.hpp>
#include <optional>
#include <memory>

namespace scene {
    class SceneGraphSystem;
}

namespace services {

    class EditorModeServiceImpl : public IEditorModeService {
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
        EditorMode currentMode = EditorMode::Edit;
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;
        std::optional<nlohmann::json> playModeSnapshot;

        void captureSnapshot();
        void restoreSnapshot();
    };

}

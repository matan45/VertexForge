#pragma once
#include "../interfaces/IEditorModeService.hpp"

namespace services {

    class EditorModeServiceImpl : public IEditorModeService {
    public:
        EditorModeServiceImpl() = default;
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
    };

}

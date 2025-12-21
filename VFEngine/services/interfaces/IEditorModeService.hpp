#pragma once
#include "../data/EditorMode.hpp"

namespace services {

    // Editor mode service interface - manages Editor/Play/Pause mode state
    // Used to disable editor-only features when in Play mode
    class IEditorModeService {
    public:
        virtual ~IEditorModeService() = default;

        // Register CQRS event handlers
        virtual void registerEventHandlers() = 0;

        // ============================================
        // Mode Control
        // ============================================

        virtual void setMode(EditorMode mode) = 0;
        virtual EditorMode getMode() const = 0;

        // ============================================
        // Convenience Queries
        // ============================================

        virtual bool isPlayMode() const = 0;
        virtual bool isEditMode() const = 0;
        virtual bool isPauseMode() const = 0;
    };

}

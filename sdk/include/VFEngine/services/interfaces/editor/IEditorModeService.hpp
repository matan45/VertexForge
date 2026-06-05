#pragma once
#include "../../data/EditorMode.hpp"

namespace services {
    
    class IEditorModeService {
    public:
        virtual ~IEditorModeService() = default;

        // Register CQRS event handlers
        virtual void registerEventHandlers() = 0;

        // Per-frame update for deferred operations (e.g. snapshot restore)
        virtual void update() {}

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

        // ============================================
        // Pause Control
        // ============================================

        virtual void setPaused(bool paused) = 0;
        virtual bool isPaused() const = 0;
    };

}

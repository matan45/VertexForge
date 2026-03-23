#pragma once
#include "../EventTypes.hpp"
#include <cstdint>

namespace events::application {

    // ============================================
    // COMMANDS - Application lifecycle operations
    // ============================================

    struct CloseCommand : ICommand<> {
        std::string_view getName() const override { return "ApplicationClose"; }
    };

    // ============================================
    // NOTIFICATIONS - Application state broadcasts
    // ============================================

    struct CloseRequestedNotification : INotification {
        std::string_view getName() const override { return "ApplicationCloseRequested"; }
    };

    // ============================================
    // NOTIFICATIONS - Window state broadcasts
    // ============================================

    struct WindowResizedNotification : INotification {
        uint32_t width;
        uint32_t height;

        std::string_view getName() const override { return "WindowResized"; }
    };

    struct WindowMinimizedNotification : INotification {
        std::string_view getName() const override { return "WindowMinimized"; }
    };

    struct WindowRestoredNotification : INotification {
        std::string_view getName() const override { return "WindowRestored"; }
    };

    struct WindowFocusedNotification : INotification {
        bool focused;

        std::string_view getName() const override { return "WindowFocused"; }
    };

    // ============================================
    // NOTIFICATIONS - UI action requests
    // ============================================

    struct OpenImportDialogNotification : INotification {
        std::string_view getName() const override { return "OpenImportDialog"; }
    };

    struct OpenInputMappingWindowNotification : INotification {
        std::string_view getName() const override { return "OpenInputMappingWindow"; }
    };

    struct OpenBackgroundRemovalNotification : INotification {
        std::string filePath;
        std::string_view getName() const override { return "OpenBackgroundRemoval"; }
    };

}

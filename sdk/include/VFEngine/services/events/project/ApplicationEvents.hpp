#pragma once
#include "../EventTypes.hpp"
#include "types/RenderSettings.hpp"
#include <cstdint>

namespace events::application {

    // ============================================
    // COMMANDS - Application lifecycle operations
    // ============================================

    struct CloseCommand : ICommand<> {
        std::string_view getName() const override { return "ApplicationClose"; }
    };

    // ============================================
    // QUERIES - Window state reads
    // ============================================

    struct GetWindowWidthQuery : IQuery<uint32_t> {
        std::string_view getName() const override { return "GetWindowWidth"; }
    };

    struct GetWindowHeightQuery : IQuery<uint32_t> {
        std::string_view getName() const override { return "GetWindowHeight"; }
    };

    struct GetViewportWidthQuery : IQuery<uint32_t> {
        std::string_view getName() const override { return "GetViewportWidth"; }
    };

    struct GetViewportHeightQuery : IQuery<uint32_t> {
        std::string_view getName() const override { return "GetViewportHeight"; }
    };

    // Monitor refresh rate in Hz (0 if unknown). Used to derive the VSync (Fifo)
    // present interval so the editor FPS can be capped to the presented rate.
    struct GetMonitorRefreshRateQuery : IQuery<uint32_t> {
        std::string_view getName() const override { return "GetMonitorRefreshRate"; }
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

    // Applied when the scene's display settings (VSync present mode) change or load.
    // Handled by the editor/runtime bootstrap to reconfigure the swapchain + rebuild.
    struct ApplyDisplaySettingsNotification : INotification {
        types::PresentMode presentMode = types::PresentMode::Mailbox;
        types::MsaaSamples msaa = types::MsaaSamples::Off;

        std::string_view getName() const override { return "ApplyDisplaySettings"; }
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

    struct OpenProjectSettingsWindowNotification : INotification {
        std::string_view getName() const override { return "OpenProjectSettingsWindow"; }
    };

    struct OpenBackgroundRemovalNotification : INotification {
        std::string filePath;
        std::string_view getName() const override { return "OpenBackgroundRemoval"; }
    };

    // VK-1435 — a UICanvas-rooted .vfPrefab was opened from the content browser. It is a UI
    // layer (Label/Image/List/...), not a character rig, so it opens in the UI Layer Builder
    // instead of the mesh-only rig Prefab Preview (which would show "no renderable rig parts").
    struct OpenUILayerBuilderNotification : INotification {
        std::string filePath;
        std::string_view getName() const override { return "OpenUILayerBuilder"; }
    };

}

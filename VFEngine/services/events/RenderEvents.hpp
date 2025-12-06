#pragma once
#include "EventTypes.hpp"
#include "../data/DTOs.hpp"
#include <optional>
#include <string>

namespace events::render {

    // ============================================
    // COMMANDS - Operations that modify render state
    // ============================================

    struct SetIBLCommand : ICommand<bool> {
        std::string hdrPath;

        std::string_view getName() const override { return "SetIBL"; }
    };

    struct RemoveIBLCommand : ICommand<> {
        std::string_view getName() const override { return "RemoveIBL"; }
    };

    struct ResizeViewportCommand : ICommand<> {
        uint32_t width;
        uint32_t height;

        std::string_view getName() const override { return "ResizeViewport"; }
    };

    struct LoadEditorTextureCommand : ICommand<services::EditorTextureHandle> {
        std::string path;
        bool isHDR = false;

        std::string_view getName() const override { return "LoadEditorTexture"; }
    };

    struct ReleaseEditorTextureCommand : ICommand<> {
        void* handle;

        std::string_view getName() const override { return "ReleaseEditorTexture"; }
    };

    // ============================================
    // QUERIES - Read-only operations
    // ============================================

    struct GetViewportTextureQuery : IQuery<services::ViewportTextureHandle> {
        std::string_view getName() const override { return "GetViewportTexture"; }
    };

    struct HasIBLQuery : IQuery<bool> {
        std::string_view getName() const override { return "HasIBL"; }
    };

    struct GetIBLPathQuery : IQuery<std::optional<std::string>> {
        std::string_view getName() const override { return "GetIBLPath"; }
    };

    // ============================================
    // NOTIFICATIONS - State change broadcasts
    // ============================================

    struct ViewportResizedNotification : INotification {
        uint32_t width;
        uint32_t height;

        std::string_view getName() const override { return "ViewportResized"; }
    };

    struct IBLChangedNotification : INotification {
        std::optional<std::string> hdrPath;  // nullopt if IBL removed

        std::string_view getName() const override { return "IBLChanged"; }
    };

    struct RenderFrameCompleteNotification : INotification {
        uint64_t frameNumber;
        double frameTimeMs;

        std::string_view getName() const override { return "RenderFrameComplete"; }
    };

}

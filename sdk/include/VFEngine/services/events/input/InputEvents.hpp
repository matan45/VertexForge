#pragma once
#include "../EventTypes.hpp"
#include "../../data/DTOs.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace events::input {

    // ============================================
    // QUERIES - Input state read-only operations
    // ============================================

    struct IsKeyDownQuery : IQuery<bool> {
        int keyCode;

        std::string_view getName() const override { return "IsKeyDown"; }
    };

    struct IsMouseButtonDownQuery : IQuery<bool> {
        int button;

        std::string_view getName() const override { return "IsMouseButtonDown"; }
    };

    struct GetMousePositionQuery : IQuery<glm::vec2> {
        std::string_view getName() const override { return "GetMousePosition"; }
    };

    struct GetViewportMousePositionQuery : IQuery<glm::vec2> {
        std::string_view getName() const override { return "GetViewportMousePosition"; }
    };

    struct GetMouseDeltaQuery : IQuery<glm::vec2> {
        std::string_view getName() const override { return "GetMouseDelta"; }
    };

    struct GetScrollDeltaQuery : IQuery<glm::vec2> {
        std::string_view getName() const override { return "GetScrollDelta"; }
    };

    struct IsKeyReleasedQuery : IQuery<bool> {
        int keyCode;

        std::string_view getName() const override { return "IsKeyReleased"; }
    };

    struct IsMouseButtonPressedQuery : IQuery<bool> {
        int button;

        std::string_view getName() const override { return "IsMouseButtonPressed"; }
    };

    struct IsMouseButtonReleasedQuery : IQuery<bool> {
        int button;

        std::string_view getName() const override { return "IsMouseButtonReleased"; }
    };

    struct IsDoubleClickQuery : IQuery<bool> {
        int button;

        std::string_view getName() const override { return "IsDoubleClick"; }
    };

    struct IsKeyPressedQuery : IQuery<bool> {
        int keyCode;

        std::string_view getName() const override { return "IsKeyPressed"; }
    };

    struct GetCharInputQuery : IQuery<std::vector<uint32_t>> {
        std::string_view getName() const override { return "GetCharInput"; }
    };

    struct GetClipboardTextQuery : IQuery<std::string> {
        std::string_view getName() const override { return "GetClipboardText"; }
    };

    struct SetClipboardTextCommand : ICommand<void> {
        std::string text;
        std::string_view getName() const override { return "SetClipboardText"; }
    };

    // ============================================
    // COMMANDS - Input control
    // ============================================

    struct SetKeyboardEnabledCommand : ICommand<void> {
        bool enabled;
        std::string_view getName() const override { return "SetKeyboardEnabled"; }
    };

    struct SetMouseEnabledCommand : ICommand<void> {
        bool enabled;
        std::string_view getName() const override { return "SetMouseEnabled"; }
    };

    struct SetCursorVisibleCommand : ICommand<void> {
        bool visible;
        std::string_view getName() const override { return "SetCursorVisible"; }
    };

    // Relative ("captured") mouse mode for editor camera look (VK-1428).
    struct SetRelativeMouseModeCommand : ICommand<void> {
        bool enabled;
        std::string_view getName() const override { return "SetRelativeMouseMode"; }
    };

    struct GetRelativeMouseDeltaQuery : IQuery<glm::vec2> {
        std::string_view getName() const override { return "GetRelativeMouseDelta"; }
    };

    struct IsRelativeMouseModeQuery : IQuery<bool> {
        std::string_view getName() const override { return "IsRelativeMouseMode"; }
    };

    struct IsKeyboardEnabledQuery : IQuery<bool> {
        std::string_view getName() const override { return "IsKeyboardEnabled"; }
    };

    struct IsMouseEnabledQuery : IQuery<bool> {
        std::string_view getName() const override { return "IsMouseEnabled"; }
    };

    struct IsCursorVisibleQuery : IQuery<bool> {
        std::string_view getName() const override { return "IsCursorVisible"; }
    };

    // ============================================
    // NOTIFICATIONS - State change broadcasts
    // ============================================

    struct KeyPressedNotification : INotification {
        int keyCode;
        bool shiftDown;
        bool ctrlDown;
        bool altDown;
        float mouseX;
        float mouseY;

        std::string_view getName() const override { return "KeyPressed"; }
    };

    struct KeyReleasedNotification : INotification {
        int keyCode;
        bool shiftDown;
        bool ctrlDown;
        bool altDown;
        float mouseX;
        float mouseY;

        std::string_view getName() const override { return "KeyReleased"; }
    };

    struct MouseButtonPressedNotification : INotification {
        int button;
        bool shiftDown;
        bool ctrlDown;
        bool altDown;
        float mouseX;
        float mouseY;

        std::string_view getName() const override { return "MouseButtonPressed"; }
    };

    struct MouseButtonReleasedNotification : INotification {
        int button;
        bool shiftDown;
        bool ctrlDown;
        bool altDown;
        float mouseX;
        float mouseY;

        std::string_view getName() const override { return "MouseButtonReleased"; }
    };

}

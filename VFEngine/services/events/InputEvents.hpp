#pragma once
#include "EventTypes.hpp"
#include "../data/DTOs.hpp"
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
    // NOTIFICATIONS - State change broadcasts
    // ============================================

    struct KeyPressedNotification : INotification {
        int keyCode;

        std::string_view getName() const override { return "KeyPressed"; }
    };

    struct KeyReleasedNotification : INotification {
        int keyCode;

        std::string_view getName() const override { return "KeyReleased"; }
    };

    struct MouseButtonPressedNotification : INotification {
        int button;

        std::string_view getName() const override { return "MouseButtonPressed"; }
    };

    struct MouseButtonReleasedNotification : INotification {
        int button;

        std::string_view getName() const override { return "MouseButtonReleased"; }
    };

}

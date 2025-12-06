#pragma once
#include "EventTypes.hpp"
#include "../data/DTOs.hpp"
#include <glm/glm.hpp>

namespace events::input {

    // ============================================
    // COMMANDS - Operations that affect input/window state
    // ============================================

    struct CloseApplicationCommand : ICommand<> {
        std::string_view getName() const override { return "CloseApplication"; }
    };

    // ============================================
    // QUERIES - Read-only operations
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

    struct ApplicationCloseRequestedNotification : INotification {
        std::string_view getName() const override { return "ApplicationCloseRequested"; }
    };

}

#pragma once
#include "EventTypes.hpp"
#include "../data/EntityHandle.hpp"
#include <glm/glm.hpp>

namespace events::controller {

    // ============================================
    // COMMANDS - State-modifying operations
    // ============================================

    struct SetMoveInputCommand : ICommand<> {
        services::EntityHandle entity;
        glm::vec3 moveInput;

        std::string_view getName() const override { return "SetMoveInput"; }
    };

    struct SetJumpCommand : ICommand<> {
        services::EntityHandle entity;
        bool wantsJump;

        std::string_view getName() const override { return "SetJump"; }
    };

    struct SetSprintCommand : ICommand<> {
        services::EntityHandle entity;
        bool wantsSprint;

        std::string_view getName() const override { return "SetSprint"; }
    };

    struct MoveToCommand : ICommand<bool> {
        services::EntityHandle entity;
        glm::vec3 destination;

        std::string_view getName() const override { return "MoveTo"; }
    };

    struct StopMovementCommand : ICommand<> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "StopMovement"; }
    };

    struct SetMoveSpeedCommand : ICommand<> {
        services::EntityHandle entity;
        float moveSpeed;

        std::string_view getName() const override { return "SetMoveSpeed"; }
    };

    struct SetJumpForceCommand : ICommand<> {
        services::EntityHandle entity;
        float jumpForce;

        std::string_view getName() const override { return "SetJumpForce"; }
    };

    struct SetSprintMultiplierCommand : ICommand<> {
        services::EntityHandle entity;
        float sprintMultiplier;

        std::string_view getName() const override { return "SetSprintMultiplier"; }
    };

    // ============================================
    // QUERIES - Read-only operations
    // ============================================

    struct HasReachedDestinationQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasReachedDestination"; }
    };

    struct GetDistanceToQuery : IQuery<float> {
        services::EntityHandle entity;
        glm::vec3 target;

        std::string_view getName() const override { return "GetDistanceTo"; }
    };

    struct GetMoveSpeedQuery : IQuery<float> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetMoveSpeed"; }
    };

    struct GetJumpForceQuery : IQuery<float> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetJumpForce"; }
    };

    struct GetSprintMultiplierQuery : IQuery<float> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetSprintMultiplier"; }
    };

}

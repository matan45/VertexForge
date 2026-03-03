#pragma once
#include "EventTypes.hpp"
#include "../data/EntityHandle.hpp"
#include <glm/glm.hpp>
#include <string>

namespace events::controller
{

    struct SetMoveInputCommand : ICommand<>
    {
        services::EntityHandle entity;
        glm::vec3 moveInput;
        std::string_view getName() const override { return "SetMoveInput"; }
    };

    struct SetJumpCommand : ICommand<>
    {
        services::EntityHandle entity;
        bool wantsJump;
        std::string_view getName() const override { return "SetJump"; }
    };

    struct SetSprintCommand : ICommand<>
    {
        services::EntityHandle entity;
        bool wantsSprint;
        std::string_view getName() const override { return "SetSprint"; }
    };

    struct MoveToCommand : ICommand<bool>
    {
        services::EntityHandle entity;
        glm::vec3 destination;
        std::string_view getName() const override { return "MoveTo"; }
    };

    struct StopMovementCommand : ICommand<>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "StopMovement"; }
    };

    struct SetMoveSpeedCommand : ICommand<>
    {
        services::EntityHandle entity;
        float moveSpeed;
        std::string_view getName() const override { return "SetMoveSpeed"; }
    };

    struct SetJumpForceCommand : ICommand<>
    {
        services::EntityHandle entity;
        float jumpForce;
        std::string_view getName() const override { return "SetJumpForce"; }
    };

    struct SetSprintMultiplierCommand : ICommand<>
    {
        services::EntityHandle entity;
        float sprintMultiplier;
        std::string_view getName() const override { return "SetSprintMultiplier"; }
    };

    struct SetArrivalDistanceCommand : ICommand<>
    {
        services::EntityHandle entity;
        float arrivalDistance;
        std::string_view getName() const override { return "SetArrivalDistance"; }
    };

    struct SetGroundedCommand : ICommand<>
    {
        services::EntityHandle entity;
        bool isGrounded;
        std::string_view getName() const override { return "SetGrounded"; }
    };

    struct HasReachedDestinationQuery : IQuery<bool>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "HasReachedDestination"; }
    };

    struct GetDistanceToQuery : IQuery<float>
    {
        services::EntityHandle entity;
        glm::vec3 target;
        std::string_view getName() const override { return "GetDistanceTo"; }
    };

    struct GetMoveSpeedQuery : IQuery<float>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "GetMoveSpeed"; }
    };

    struct GetJumpForceQuery : IQuery<float>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "GetJumpForce"; }
    };

    struct GetSprintMultiplierQuery : IQuery<float>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "GetSprintMultiplier"; }
    };

    struct GetArrivalDistanceQuery : IQuery<float>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "GetArrivalDistance"; }
    };

    struct IsGroundedQuery : IQuery<bool>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "IsGrounded"; }
    };

    struct SetAccelerationCommand : ICommand<>
    {
        services::EntityHandle entity;
        float acceleration;
        std::string_view getName() const override { return "SetAcceleration"; }
    };

    struct SetDecelerationCommand : ICommand<>
    {
        services::EntityHandle entity;
        float deceleration;
        std::string_view getName() const override { return "SetDeceleration"; }
    };

    struct SetRotationSpeedCommand : ICommand<>
    {
        services::EntityHandle entity;
        float rotationSpeed;
        std::string_view getName() const override { return "SetRotationSpeed"; }
    };

    struct SetAirControlFactorCommand : ICommand<>
    {
        services::EntityHandle entity;
        float airControlFactor;
        std::string_view getName() const override { return "SetAirControlFactor"; }
    };

    struct SetWalkSpeedThresholdCommand : ICommand<>
    {
        services::EntityHandle entity;
        float walkSpeedThreshold;
        std::string_view getName() const override { return "SetWalkSpeedThreshold"; }
    };

    struct SetLocomotionStateCommand : ICommand<>
    {
        services::EntityHandle entity;
        std::string state;
        std::string_view getName() const override { return "SetLocomotionState"; }
    };

    struct GetLocomotionStateQuery : IQuery<std::string>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "GetLocomotionState"; }
    };

    struct GetCurrentSpeedQuery : IQuery<float>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "GetCurrentSpeed"; }
    };

    struct GetVerticalVelocityQuery : IQuery<float>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "GetVerticalVelocity"; }
    };

    struct GetAccelerationQuery : IQuery<float>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "GetAcceleration"; }
    };

    struct GetDecelerationQuery : IQuery<float>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "GetDeceleration"; }
    };

    struct GetRotationSpeedQuery : IQuery<float>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "GetRotationSpeed"; }
    };

    struct GetAirControlFactorQuery : IQuery<float>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "GetAirControlFactor"; }
    };

    struct GetWalkSpeedThresholdQuery : IQuery<float>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "GetWalkSpeedThreshold"; }
    };

}

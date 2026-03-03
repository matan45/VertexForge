#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace components
{
    enum class LocomotionParamSource : uint8_t
    {
        Speed,
        Grounded,
        VerticalVelocity,
        DirectionX,
        DirectionY
    };

    struct LocomotionParamMapping
    {
        LocomotionParamSource source;
        std::string paramName;
    };

    struct LocomotionConfig
    {
        bool syncToAnimator = true;

        std::string idleState = "Idle";
        std::string walkState = "Walk";
        std::string runState = "Run";
        std::string jumpState = "Jump";
        std::string fallState = "Fall";

        std::vector<LocomotionParamMapping> paramMappings = {
            {LocomotionParamSource::Speed, "speed"},
            {LocomotionParamSource::Grounded, "grounded"},
            {LocomotionParamSource::VerticalVelocity, "verticalVelocity"},
            {LocomotionParamSource::DirectionX, "directionX"},
            {LocomotionParamSource::DirectionY, "directionY"}
        };
    };

    struct ControllerComponent
    {
        float moveSpeed = 5.0f;
        float sprintMultiplier = 1.5f;
        float jumpForce = 5.0f;
        float arrivalDistance = 0.5f;

        float acceleration = 10.0f;
        float deceleration = 15.0f;
        float rotationSpeed = 720.0f;
        float airControlFactor = 0.3f;
        float walkSpeedThreshold = 2.5f;

        float stepHeight = 0.35f;
        float maxSlopeAngle = 45.0f;

        LocomotionConfig locomotionConfig;

        glm::vec3 moveInput{0.0f};
        bool wantsJump = false;
        bool wantsSprint = false;

        bool isGrounded = false;
        glm::vec3 currentVelocity{0.0f};
        float currentSpeed = 0.0f;
        float verticalVelocity = 0.0f;
        std::string locomotionState = "Idle";

        bool characterControllerActive = false;

        bool hasMoveToTarget = false;
        bool moveToDestinationDirty = false;
        glm::vec3 moveToDestination{0.0f};
    };
}

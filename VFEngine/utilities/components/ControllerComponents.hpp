#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace components
{
    enum class LocomotionState : uint8_t
    {
        Idle,
        Walk,
        Run,
        Jump,
        Fall
    };

    enum class LocomotionParamSource : uint8_t
    {
        Speed,
        Grounded,
        VerticalVelocity,
        DirectionX,
        DirectionY
    };

    struct LocomotionStateMapping
    {
        LocomotionState state;
        std::string name;
    };

    struct LocomotionParamMapping
    {
        LocomotionParamSource source;
        std::string paramName;
    };

    struct LocomotionConfig
    {
        bool syncToAnimator = true;

        std::vector<LocomotionStateMapping> stateNames = {
            {LocomotionState::Idle, "Idle"},
            {LocomotionState::Walk, "Walk"},
            {LocomotionState::Run, "Run"},
            {LocomotionState::Jump, "Jump"},
            {LocomotionState::Fall, "Fall"}
        };

        std::vector<LocomotionParamMapping> paramMappings = {
            {LocomotionParamSource::Speed, "speed"},
            {LocomotionParamSource::Grounded, "grounded"},
            {LocomotionParamSource::VerticalVelocity, "verticalVelocity"},
            {LocomotionParamSource::DirectionX, "directionX"},
            {LocomotionParamSource::DirectionY, "directionY"}
        };

        std::string getStateName(LocomotionState state) const
        {
            for (const auto& mapping : stateNames)
            {
                if (mapping.state == state) return mapping.name;
            }
            return {};
        }
    };

    struct ControllerComponent
    {
        // === Serialized fields ===

        // Movement
        float moveSpeed = 5.0f;
        float sprintMultiplier = 1.5f;
        float jumpForce = 5.0f;
        float arrivalDistance = 0.5f;

        // Locomotion
        float acceleration = 10.0f;
        float deceleration = 15.0f;
        float rotationSpeed = 720.0f;
        float airControlFactor = 0.3f;
        float walkSpeedThreshold = 2.5f;

        // Character controller behavior (shape comes from ColliderComponent)
        float stepHeight = 0.35f;
        float maxSlopeAngle = 45.0f;

        // Animator sync configuration
        LocomotionConfig locomotionConfig;

        // === Runtime state (not serialized) ===

        // Script-driven input (set each frame)
        glm::vec3 moveInput{0.0f};
        bool wantsJump = false;
        bool wantsSprint = false;

        // Physics state (set by character controller)
        bool isGrounded = false;
        glm::vec3 currentVelocity{0.0f};
        float currentSpeed = 0.0f;
        float verticalVelocity = 0.0f;
        LocomotionState locomotionState = LocomotionState::Idle;

        // Whether a CharacterVirtual is active for this entity (managed by PhysicsWorld, false when not in play mode)
        bool characterControllerActive = false;

        // Navigation
        bool hasMoveToTarget = false;
        bool moveToDestinationDirty = false;
        glm::vec3 moveToDestination{0.0f};
    };
}

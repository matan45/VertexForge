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

        // Auto-derived state names (index 0=idle, 1=walk, 2=run, 3=jump, 4=fall)
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
        std::string locomotionState = "Idle";

        // Whether a CharacterVirtual is active for this entity (managed by PhysicsWorld, false when not in play mode)
        bool characterControllerActive = false;

        // Navigation
        bool hasMoveToTarget = false;
        bool moveToDestinationDirty = false;
        glm::vec3 moveToDestination{0.0f};
    };
}

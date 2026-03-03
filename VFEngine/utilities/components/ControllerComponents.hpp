#pragma once
#include <glm/glm.hpp>
#include <cstdint>

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

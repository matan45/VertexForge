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
        std::string swimState = "Swim";   // VK-1606

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

        // VK-1606: swimming. Off by default, so an existing character keeps sinking exactly as it
        // did before. Only character-controller entities use these - a rigid body gets its buoyancy
        // from OceanService::updateBuoyancy instead, and the two never overlap because a controller
        // is a Jolt CharacterVirtual with no RigidBodyComponent.
        bool swimEnabled = false;
        float swimSpeed = 3.0f;             // horizontal speed while swimming (replaces moveSpeed)
        float swimUpSpeed = 2.0f;           // vertical speed the jump input gives while submerged
        float waterDrag = 3.0f;             // per-second velocity decay in water
        float floatDepth = 1.2f;            // metres of the capsule that sit below the surface
        float swimBuoyancyStiffness = 36.0f;// spring constant pulling toward floatDepth
        float swimEnterSubmersion = 0.6f;   // submersion fraction at which wading becomes swimming

        LocomotionConfig locomotionConfig;

        glm::vec3 moveInput{0.0f};
        bool wantsJump = false;
        bool wantsSprint = false;

        bool isGrounded = false;
        glm::vec3 currentVelocity{0.0f};
        float currentSpeed = 0.0f;
        float verticalVelocity = 0.0f;
        std::string locomotionState = "Idle";

        // VK-1606 runtime state, written by ControllerServiceImpl each frame.
        bool isSwimming = false;
        float submersion = 0.0f;            // 0..1 fraction of the capsule below the surface

        bool characterControllerActive = false;

        bool hasMoveToTarget = false;
        bool moveToDestinationDirty = false;
        glm::vec3 moveToDestination{0.0f};
    };
}

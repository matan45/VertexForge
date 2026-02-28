#pragma once
#include <glm/glm.hpp>

namespace components
{
    struct ControllerComponent
    {
        // Serialized fields
        float moveSpeed = 5.0f;
        float sprintMultiplier = 1.5f;
        float jumpForce = 5.0f;
        float arrivalDistance = 0.5f;

        // Runtime state (set by scripts each frame, not serialized)
        glm::vec3 moveInput{0.0f};
        bool wantsJump = false;
        bool wantsSprint = false;
        bool isGrounded = false;
        bool hasMoveToTarget = false;
        bool moveToDestinationDirty = false;
        glm::vec3 moveToDestination{0.0f};
    };
}

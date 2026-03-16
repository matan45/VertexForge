#pragma once
#include <glm/glm.hpp>
#include <string>
#include "../asset/AssetRef.hpp"

namespace components
{
    struct NavmeshComponent
    {
        asset::AssetRef navmeshRef;
    };

    struct NavmeshAgentComponent
    {
        float radius = 0.25f;
        float height = 2.0f;
        float maxSpeed = 3.5f;
        float maxAcceleration = 8.0f;
        float stoppingDistance = 0.1f;

        // Arrival and stuck detection
        float arrivalDistance = 1.0f;
        float stuckVelocityThreshold = 0.1f;
        float stuckTimeThreshold = 0.5f;

        // Avoidance
        uint8_t avoidanceQuality = 3;       // 0-3, higher = better but slower
        float separationWeight = 2.0f;

        // Runtime state (not serialized)
        bool isActive = false;
        bool isSuspended = false;
        int crowdAgentIndex = -1;

        // Stored state for suspend/resume
        glm::vec3 suspendedPosition{0.0f};
        glm::vec3 suspendedTarget{0.0f};
        bool hasSuspendedTarget = false;
    };
}

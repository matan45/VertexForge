#pragma once
#include "../types/PhysicsAnimationTypes.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace components
{
    struct PhysicsAnimationComponent
    {
        // Config data (serialized)
        std::string physicsAnimationPath;
        types::PhysicsAnimationConfig config;

        // Runtime state (transient, NOT serialized)
        types::PhysicsAnimationMode currentMode = types::PhysicsAnimationMode::Animated;
        bool isInitialized = false;
        float transitionProgress = 0.0f;
        uint32_t ragdollCollisionGroup = 0;

        // Ragdoll-derived bone matrices for GPU override (transient)
        std::vector<glm::mat4> overrideBoneMatrices;
    };
}

#pragma once
#include "../types/PhysicsAnimationTypes.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>
#include "../asset/AssetRef.hpp"

namespace components
{
    struct PhysicsAnimationComponent
    {
        // Config data (serialized)
        asset::AssetRef physicsAnimationRef;
        types::PhysicsAnimationConfig config;

        // Runtime state (transient, NOT serialized)
        types::PhysicsAnimationMode currentMode = types::PhysicsAnimationMode::Animated;
        bool isInitialized = false;
        float transitionProgress = 0.0f;
        uint32_t ragdollCollisionGroup = 0;

        // Powered ragdoll runtime state (transient)
        float globalMotorStrength = 1.0f;
        bool ragdollSettled = false;

        // Ragdoll-derived bone matrices for GPU override (transient)
        std::vector<glm::mat4> overrideBoneMatrices;

        // Pose snapshot used to crossfade ragdoll -> animated (transient)
        std::vector<glm::mat4> capturedPoseMatrices;
        float blendOutProgress = 1.0f; // 1 = no blend-out in flight
    };
}

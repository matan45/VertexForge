#pragma once

#include "../animator/IKTypes.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <optional>
#include <cstdint>

namespace components
{
    struct IKChainConfig
    {
        std::string chainName;
        std::string tipBoneName;
        std::vector<std::string> chainBoneNames; // root-to-tip order
        std::vector<animator::ik::JointConstraint> constraints; // per-bone constraints
        float weight = 1.0f;
        bool enabled = true;
    };

    struct IKChainRuntimeState
    {
        // Resolved bone indices (cached from bone names)
        std::vector<int32_t> resolvedBoneIndices;
        int32_t resolvedTipIndex = -1;

        // Current target (set at runtime by code/script)
        glm::vec3 targetPosition{0.0f};
        std::optional<glm::quat> targetRotation;
        float currentWeight = 0.0f;
        bool isActive = false;

        // Smoothing state
        glm::vec3 smoothedTargetPosition{0.0f};
        bool hasSmoothedTarget = false;
    };

    struct IKTargetComponent
    {
        std::vector<IKChainConfig> chains;

        // Transient runtime state (NOT serialized)
        std::vector<IKChainRuntimeState> runtimeStates;
        bool isInitialized = false;
    };
}

#pragma once

#include "../animator/IKTypes.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <optional>
#include <cstdint>

namespace components
{
    struct IKChainRuntimeState
    {
        // Resolved bone indices (cached from bone names)
        std::vector<int32_t> resolvedBoneIndices;
        int32_t resolvedTipIndex = -1;

        glm::vec3 targetPosition{0.0f};
        std::optional<glm::quat> targetRotation;
        float currentWeight = 0.0f;
        bool isActive = false;
    };

    struct IKTargetComponent
    {
        std::vector<animator::ik::IKChainConfig> chains;

        // Transient runtime state (NOT serialized)
        std::vector<IKChainRuntimeState> runtimeStates;
        bool isInitialized = false;
    };
}

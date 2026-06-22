#pragma once

#include "AnimationExport.hpp"
#include "IKSolver.hpp"
#include "animator/IKTypes.hpp"
#include "components/IKComponent.hpp"
#include "resource/Types.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace animation
{
    class VF_ANIMATION_API IKPostProcessor
    {
    public:
        // entityWorldMatrix maps the skeleton's MODEL space to the entity's WORLD
        // space. IK targets are supplied in world space (e.g. a socket world
        // position from script) but the solver runs in model space, so the target
        // is converted by inverse(entityWorldMatrix). Pass identity for an entity
        // already at the origin with unit scale.
        static void applyIK(
            std::vector<glm::mat4>& boneMatrices,
            const resource::SkeletonData& skeleton,
            std::vector<animator::ik::IKChainConfig>& chains,
            std::vector<components::IKChainRuntimeState>& runtimeStates,
            const glm::mat4& entityWorldMatrix = glm::mat4(1.0f)
        );

    private:
        static void initializeChainRuntime(
            const animator::ik::IKChainConfig& config,
            const resource::SkeletonData& skeleton,
            components::IKChainRuntimeState& state
        );

        static void solveChain(
            std::vector<glm::mat4>& boneMatrices,
            const resource::SkeletonData& skeleton,
            const animator::ik::IKChainConfig& config,
            const components::IKChainRuntimeState& state,
            const glm::mat4& worldToModel
        );
    };
}

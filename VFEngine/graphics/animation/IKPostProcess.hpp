#pragma once

#include "IKSolver.hpp"
#include "animator/IKTypes.hpp"
#include "components/IKComponent.hpp"
#include "resource/Types.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace animation
{
    class IKPostProcessor
    {
    public:
        static void applyIK(
            std::vector<glm::mat4>& boneMatrices,
            const resource::SkeletonData& skeleton,
            std::vector<animator::ik::IKChainConfig>& chains,
            std::vector<components::IKChainRuntimeState>& runtimeStates
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
            const components::IKChainRuntimeState& state
        );

        // Convert skinning matrices to bone world transforms
        static std::vector<glm::mat4> skinningToWorld(
            const std::vector<glm::mat4>& skinningMatrices,
            const resource::SkeletonData& skeleton
        );

        // Recompute hierarchy from a modified bone downward
        static void recomputeHierarchy(
            std::vector<glm::mat4>& worldTransforms,
            const resource::SkeletonData& skeleton,
            const std::vector<int32_t>& affectedBoneIndices
        );

        // Convert world transforms back to skinning matrices
        static void worldToSkinning(
            const std::vector<glm::mat4>& worldTransforms,
            const resource::SkeletonData& skeleton,
            std::vector<glm::mat4>& outSkinningMatrices
        );
    };
}

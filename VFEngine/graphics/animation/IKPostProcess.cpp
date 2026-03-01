#include "IKPostProcess.hpp"
#include "print/Logger.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <unordered_set>

namespace animation
{
    void IKPostProcessor::applyIK(
        std::vector<glm::mat4>& boneMatrices,
        const resource::SkeletonData& skeleton,
        std::vector<animator::ik::IKChainConfig>& chains,
        std::vector<components::IKChainRuntimeState>& runtimeStates)
    {
        if (chains.empty() || boneMatrices.empty())
            return;

        if (runtimeStates.size() != chains.size())
            runtimeStates.resize(chains.size());

        bool anyActive = false;
        for (size_t i = 0; i < chains.size(); ++i)
        {
            auto& state = runtimeStates[i];

            if (state.resolvedTipIndex < 0)
                initializeChainRuntime(chains[i], skeleton, state);

            if (state.isActive && chains[i].enabled && state.currentWeight > 0.0f)
                anyActive = true;
        }

        if (!anyActive)
            return;

        for (size_t i = 0; i < chains.size(); ++i)
        {
            const auto& config = chains[i];
            const auto& state = runtimeStates[i];

            if (!state.isActive || !config.enabled || state.currentWeight <= 0.0f)
                continue;

            if (state.resolvedBoneIndices.size() < 2 || state.resolvedTipIndex < 0)
                continue;

            solveChain(boneMatrices, skeleton, config, state);
        }
    }

    void IKPostProcessor::initializeChainRuntime(
        const animator::ik::IKChainConfig& config,
        const resource::SkeletonData& skeleton,
        components::IKChainRuntimeState& state)
    {
        state.resolvedBoneIndices.clear();
        state.resolvedTipIndex = -1;

        for (const auto& boneName : config.chainBoneNames)
        {
            int32_t idx = skeleton.getBoneIndex(boneName);
            if (idx < 0)
            {
                loggerWarning("[IKPostProcessor] Bone '{}' not found in skeleton for chain '{}'",
                              boneName, config.chainName);
                state.resolvedBoneIndices.clear();
                return;
            }
            state.resolvedBoneIndices.push_back(idx);
        }

        state.resolvedTipIndex = skeleton.getBoneIndex(config.tipBoneName);
        if (state.resolvedTipIndex < 0)
        {
            loggerWarning("[IKPostProcessor] Tip bone '{}' not found in skeleton for chain '{}'",
                          config.tipBoneName, config.chainName);
        }

        if (!state.resolvedBoneIndices.empty() &&
            state.resolvedBoneIndices.back() != state.resolvedTipIndex &&
            state.resolvedTipIndex >= 0)
        {
            state.resolvedBoneIndices.push_back(state.resolvedTipIndex);
        }
    }

    void IKPostProcessor::solveChain(
        std::vector<glm::mat4>& boneMatrices,
        const resource::SkeletonData& skeleton,
        const animator::ik::IKChainConfig& config,
        const components::IKChainRuntimeState& state)
    {
        const auto& chainIndices = state.resolvedBoneIndices;
        const size_t chainLen = chainIndices.size();

        if (chainLen < 2)
            return;

        // Step 1: Convert skinning matrices to world transforms
        //   skinningMatrix = globalInv * worldTransform * inverseBindPose
        //   worldTransform = globalInvInverse * skinningMatrix * bindPose
        const glm::mat4 globalInvInverse = glm::inverse(skeleton.globalInverseTransform);

        std::vector<glm::vec3> chainPositions(chainLen);
        std::vector<glm::quat> chainRotations(chainLen);
        std::vector<glm::vec3> chainScales(chainLen);
        std::vector<float> boneLengths(chainLen - 1);

        for (size_t i = 0; i < chainLen; ++i)
        {
            int32_t boneIdx = chainIndices[i];
            glm::mat4 worldTransform = globalInvInverse * boneMatrices[boneIdx] *
                                        skeleton.bindPoses[boneIdx];

            chainPositions[i] = glm::vec3(worldTransform[3]);

            glm::vec3 translation, skew;
            glm::vec4 perspective;
            glm::decompose(worldTransform, chainScales[i], chainRotations[i],
                           translation, skew, perspective);
        }

        for (size_t i = 0; i < chainLen - 1; ++i)
        {
            boneLengths[i] = glm::length(chainPositions[i + 1] - chainPositions[i]);
            if (boneLengths[i] < 1e-6f)
                boneLengths[i] = 0.01f; // Prevent zero-length bones
        }

        // Step 2: Prepare solver input
        FABRIKSolver::ChainInput solverInput;
        solverInput.positions = chainPositions;
        solverInput.rotations = chainRotations;
        solverInput.boneLengths = boneLengths;

        solverInput.constraints.resize(chainLen);
        for (size_t i = 0; i < chainLen && i < config.constraints.size(); ++i)
        {
            solverInput.constraints[i] = config.constraints[i];
        }

        animator::ik::IKTarget target;
        target.position = state.targetPosition;
        target.rotation = state.targetRotation;

        // Step 3: Solve
        auto solverResult = FABRIKSolver::solve(solverInput, target);

        // Step 4: Blend and apply results
        float weight = config.weight * state.currentWeight;
        weight = glm::clamp(weight, 0.0f, 1.0f);

        // Compute all world transforms from the current skinning matrices
        // Reuse a scratch buffer to avoid per-frame heap allocation
        static thread_local std::vector<glm::mat4> worldTransforms;
        const size_t boneCount = skeleton.bones.size();
        worldTransforms.resize(boneCount);
        for (size_t i = 0; i < boneCount; ++i)
        {
            worldTransforms[i] = globalInvInverse * boneMatrices[i] * skeleton.bindPoses[i];
        }

        // Apply solved positions/rotations to the chain bones with blending
        // Reuse cached rotations and scales from Step 1 to avoid a second decompose
        for (size_t i = 0; i < chainLen; ++i)
        {
            int32_t boneIdx = chainIndices[i];

            glm::vec3 originalPos = glm::vec3(worldTransforms[boneIdx][3]);
            glm::vec3 blendedPos = glm::mix(originalPos, solverResult.positions[i], weight);
            glm::quat blendedRot = glm::slerp(chainRotations[i], solverResult.rotations[i], weight);

            worldTransforms[boneIdx] = glm::translate(glm::mat4(1.0f), blendedPos) *
                                        glm::mat4_cast(blendedRot) *
                                        glm::scale(glm::mat4(1.0f), chainScales[i]);
        }

        // Step 5: Recompute children of affected bones
        // Build set of affected bone indices for quick lookup
        std::unordered_set<int32_t> affectedBones(chainIndices.begin(), chainIndices.end());

        // Precondition: skeleton bones are in topological order (parent index < child index),
        // guaranteed by ensureParentBeforeChildOrder() during mesh import.
        for (size_t i = 0; i < boneCount; ++i)
        {
            int32_t idx = static_cast<int32_t>(i);
            if (affectedBones.count(idx))
                continue;

            int32_t parentIdx = skeleton.bones[i].parentIndex;
            if (parentIdx >= 0 && affectedBones.count(parentIdx))
            {
                // Recover local transform from the original (pre-IK) world transforms
                glm::mat4 origParentWorld = globalInvInverse * boneMatrices[parentIdx] *
                                             skeleton.bindPoses[parentIdx];
                glm::mat4 origChildWorld = globalInvInverse * boneMatrices[idx] *
                                            skeleton.bindPoses[idx];
                glm::mat4 localTransform = glm::inverse(origParentWorld) * origChildWorld;

                worldTransforms[idx] = worldTransforms[parentIdx] * localTransform;
                affectedBones.insert(idx);
            }
        }

        // Step 6: Convert back to skinning matrices (reuse affectedBones set)
        for (int32_t idx : affectedBones)
        {
            boneMatrices[idx] = skeleton.globalInverseTransform *
                                 worldTransforms[idx] *
                                 skeleton.inverseBindPoses[idx];
        }
    }

}

#include "IKPostProcess.hpp"
#include "print/Logger.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <algorithm>
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

        // Ensure runtime states match chain count
        if (runtimeStates.size() != chains.size())
            runtimeStates.resize(chains.size());

        bool anyActive = false;
        for (size_t i = 0; i < chains.size(); ++i)
        {
            auto& state = runtimeStates[i];

            // Initialize chain if needed
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

        // Add tip to chain if not already included
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

        // Extract world-space positions for chain bones
        std::vector<glm::vec3> chainPositions(chainLen);
        std::vector<glm::quat> chainRotations(chainLen);
        std::vector<float> boneLengths(chainLen - 1);

        for (size_t i = 0; i < chainLen; ++i)
        {
            int32_t boneIdx = chainIndices[i];
            glm::mat4 worldTransform = globalInvInverse * boneMatrices[boneIdx] *
                                        skeleton.bindPoses[boneIdx];

            chainPositions[i] = glm::vec3(worldTransform[3]);

            glm::vec3 scale, translation, skew;
            glm::vec4 perspective;
            glm::quat rotation;
            glm::decompose(worldTransform, scale, rotation, translation, skew, perspective);
            chainRotations[i] = rotation;
        }

        // Compute bone lengths from positions
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

        // Map constraints to chain bones
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
        const size_t boneCount = skeleton.bones.size();
        std::vector<glm::mat4> worldTransforms(boneCount);
        for (size_t i = 0; i < boneCount; ++i)
        {
            worldTransforms[i] = globalInvInverse * boneMatrices[i] * skeleton.bindPoses[i];
        }

        // Apply solved positions/rotations to the chain bones with blending
        for (size_t i = 0; i < chainLen; ++i)
        {
            int32_t boneIdx = chainIndices[i];

            glm::vec3 originalPos = glm::vec3(worldTransforms[boneIdx][3]);
            glm::vec3 solvedPos = solverResult.positions[i];
            glm::vec3 blendedPos = glm::mix(originalPos, solvedPos, weight);

            glm::vec3 scale, translation, skew;
            glm::vec4 perspective;
            glm::quat originalRot;
            glm::decompose(worldTransforms[boneIdx], scale, originalRot, translation, skew, perspective);

            glm::quat solvedRot = solverResult.rotations[i];
            glm::quat blendedRot = glm::slerp(originalRot, solvedRot, weight);

            // Reconstruct world transform
            worldTransforms[boneIdx] = glm::translate(glm::mat4(1.0f), blendedPos) *
                                        glm::mat4_cast(blendedRot) *
                                        glm::scale(glm::mat4(1.0f), scale);
        }

        // Step 5: Recompute children of affected bones
        // Build set of affected bone indices for quick lookup
        std::unordered_set<int32_t> affectedBones(chainIndices.begin(), chainIndices.end());

        // Find all children that need to be updated (bones that have a parent in the chain)
        for (size_t i = 0; i < boneCount; ++i)
        {
            int32_t idx = static_cast<int32_t>(i);
            if (affectedBones.count(idx))
                continue; // Already handled

            // Check if any ancestor is in the chain
            int32_t parent = skeleton.bones[i].parentIndex;
            bool needsUpdate = false;
            while (parent >= 0)
            {
                if (affectedBones.count(parent))
                {
                    needsUpdate = true;
                    break;
                }
                parent = skeleton.bones[parent].parentIndex;
            }

            if (needsUpdate)
            {
                // Recompute this bone's world transform from its parent
                // First recover the local transform
                int32_t parentIdx = skeleton.bones[i].parentIndex;
                glm::mat4 parentWorldOrig = globalInvInverse * boneMatrices[parentIdx] *
                                             skeleton.bindPoses[parentIdx];
                glm::mat4 localTransform = glm::inverse(parentWorldOrig) *
                                            (globalInvInverse * boneMatrices[idx] * skeleton.bindPoses[idx]);

                // Apply with updated parent
                worldTransforms[idx] = worldTransforms[parentIdx] * localTransform;
            }
        }

        // Step 6: Convert back to skinning matrices
        for (size_t i = 0; i < boneCount; ++i)
        {
            int32_t idx = static_cast<int32_t>(i);
            bool isAffected = affectedBones.count(idx) > 0;

            if (!isAffected)
            {
                // Check if it's a child that was recomputed
                int32_t parent = skeleton.bones[i].parentIndex;
                while (parent >= 0 && !isAffected)
                {
                    if (affectedBones.count(parent))
                        isAffected = true;
                    parent = skeleton.bones[parent].parentIndex;
                }
            }

            if (isAffected)
            {
                boneMatrices[idx] = skeleton.globalInverseTransform *
                                     worldTransforms[idx] *
                                     skeleton.inverseBindPoses[idx];
            }
        }
    }

    std::vector<glm::mat4> IKPostProcessor::skinningToWorld(
        const std::vector<glm::mat4>& skinningMatrices,
        const resource::SkeletonData& skeleton)
    {
        const glm::mat4 globalInvInverse = glm::inverse(skeleton.globalInverseTransform);
        const size_t count = skinningMatrices.size();

        std::vector<glm::mat4> worldTransforms(count);
        for (size_t i = 0; i < count; ++i)
        {
            worldTransforms[i] = globalInvInverse * skinningMatrices[i] * skeleton.bindPoses[i];
        }

        return worldTransforms;
    }

    void IKPostProcessor::worldToSkinning(
        const std::vector<glm::mat4>& worldTransforms,
        const resource::SkeletonData& skeleton,
        std::vector<glm::mat4>& outSkinningMatrices)
    {
        const size_t count = worldTransforms.size();
        outSkinningMatrices.resize(count);

        for (size_t i = 0; i < count; ++i)
        {
            outSkinningMatrices[i] = skeleton.globalInverseTransform *
                                      worldTransforms[i] *
                                      skeleton.inverseBindPoses[i];
        }
    }

    void IKPostProcessor::recomputeHierarchy(
        std::vector<glm::mat4>& worldTransforms,
        const resource::SkeletonData& skeleton,
        const std::vector<int32_t>& affectedBoneIndices)
    {
        std::unordered_set<int32_t> affected(affectedBoneIndices.begin(), affectedBoneIndices.end());

        for (size_t i = 0; i < skeleton.bones.size(); ++i)
        {
            int32_t parent = skeleton.bones[i].parentIndex;
            if (parent >= 0 && affected.count(parent) && !affected.count(static_cast<int32_t>(i)))
            {
                glm::mat4 localTransform = glm::inverse(worldTransforms[parent]) * worldTransforms[i];
                worldTransforms[i] = worldTransforms[parent] * localTransform;
                affected.insert(static_cast<int32_t>(i));
            }
        }
    }
}

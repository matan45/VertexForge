#include "AnimationEvaluator.hpp"
#include "print/Logger.hpp"
#include <algorithm>

namespace controllers
{
    void AnimationEvaluator::loadAnimation(const resource::AnimationData& animation,
                                           const resource::SkeletonInfo& skeleton)
    {
        animationData = &animation;
        meshSkeleton = &skeleton;

        buildBoneToChannelMap();
        buildSkeletonMapping();

        // Initialize evaluated bones for animation skeleton
        evaluatedBones.resize(animation.skeleton.size());

        // Debug: count successful mappings
        size_t mappedCount = 0;
        for (int32_t idx : meshBoneToAnimBone)
        {
            if (idx >= 0) mappedCount++;
        }
        loggerInfo("Bone mapping: {} mesh bones, {} anim bones, {} mapped",
            skeleton.boneNames.size(), animation.skeleton.size(), mappedCount);

        // Log global inverse transform
        loggerInfo("=== GLOBAL INVERSE TRANSFORM ===");
        const auto& git = animation.globalInverseTransform;
        loggerInfo("  diag=({:.4f},{:.4f},{:.4f},{:.4f})",
            git[0][0], git[1][1], git[2][2], git[3][3]);
        loggerInfo("  trans=({:.4f},{:.4f},{:.4f})",
            git[3][0], git[3][1], git[3][2]);
        bool isIdentity = true;
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                float expected = (c == r) ? 1.0f : 0.0f;
                if (std::abs(git[c][r] - expected) > 0.0001f) isIdentity = false;
            }
        }
        loggerInfo("  Global inverse is: {}", isIdentity ? "IDENTITY" : "NON-IDENTITY");

        // Debug: Verify inverse bind poses are unique
        loggerInfo("=== Verifying Inverse Bind Poses ===");
        for (size_t i = 0; i < std::min(skeleton.inverseBindPoses.size(), static_cast<size_t>(5)); ++i)
        {
            const auto& invBind = skeleton.inverseBindPoses[i];
            loggerInfo("InvBindPose[{}] '{}': diag=({:.4f}, {:.4f}, {:.4f}, {:.4f}) trans=({:.4f}, {:.4f}, {:.4f})",
                i, skeleton.boneNames[i],
                invBind[0][0], invBind[1][1], invBind[2][2], invBind[3][3],
                invBind[3][0], invBind[3][1], invBind[3][2]);
        }

        // Debug: Log first few bone names from both skeletons
        loggerInfo("First mesh skeleton bones:");
        for (size_t i = 0; i < std::min(skeleton.boneNames.size(), static_cast<size_t>(5)); ++i)
        {
            loggerInfo("  Mesh bone {}: '{}'", i, skeleton.boneNames[i]);
        }
        loggerInfo("First animation skeleton bones:");
        for (size_t i = 0; i < std::min(animation.skeleton.size(), static_cast<size_t>(5)); ++i)
        {
            loggerInfo("  Anim bone {}: '{}' (parent: {})", i, animation.skeleton[i].name, animation.skeleton[i].parentIndex);
        }

        // Detailed bone mapping verification for diagnosis
        loggerInfo("=== DETAILED BONE MAPPING VERIFICATION ===");
        for (size_t meshIdx = 0; meshIdx < meshSkeleton->boneNames.size(); ++meshIdx)
        {
            int32_t animIdx = meshBoneToAnimBone[meshIdx];
            const auto& meshBoneName = meshSkeleton->boneNames[meshIdx];
            const auto& invBind = meshSkeleton->inverseBindPoses[meshIdx];

            loggerInfo("MeshBone[{}] '{}' -> AnimBone[{}]",
                meshIdx, meshBoneName, animIdx);
            loggerInfo("  InvBindPose: diag=({:.4f},{:.4f},{:.4f},{:.4f}) trans=({:.4f},{:.4f},{:.4f})",
                invBind[0][0], invBind[1][1], invBind[2][2], invBind[3][3],
                invBind[3][0], invBind[3][1], invBind[3][2]);

            if (animIdx >= 0 && animIdx < static_cast<int32_t>(animation.skeleton.size()))
            {
                const auto& animBone = animation.skeleton[animIdx];
                loggerInfo("  AnimBone '{}' parent={}", animBone.name, animBone.parentIndex);
                loggerInfo("  offsetMatrix: diag=({:.4f},{:.4f},{:.4f},{:.4f}) trans=({:.4f},{:.4f},{:.4f})",
                    animBone.offsetMatrix[0][0], animBone.offsetMatrix[1][1],
                    animBone.offsetMatrix[2][2], animBone.offsetMatrix[3][3],
                    animBone.offsetMatrix[3][0], animBone.offsetMatrix[3][1], animBone.offsetMatrix[3][2]);
                loggerInfo("  preTransform: diag=({:.4f},{:.4f},{:.4f},{:.4f}) trans=({:.4f},{:.4f},{:.4f})",
                    animBone.preTransform[0][0], animBone.preTransform[1][1],
                    animBone.preTransform[2][2], animBone.preTransform[3][3],
                    animBone.preTransform[3][0], animBone.preTransform[3][1], animBone.preTransform[3][2]);
            }
            else
            {
                loggerWarning("  WARNING: No matching animation bone found!");
            }
        }
        loggerInfo("=== END BONE MAPPING VERIFICATION ===");

        // Compute bind pose correction by evaluating animation at time=0
        // This uses the actual keyframe values at t=0, not the skeleton's offsetMatrix
        loggerInfo("=== COMPUTING BIND POSE CORRECTION (using t=0 keyframes) ===");
        bindPoseCorrection.resize(meshSkeleton->boneNames.size(), glm::mat4(1.0f));

        // First, evaluate the animation at time=0 to get actual world transforms
        // We need to compute this WITHOUT applying any correction
        for (size_t i = 0; i < animationData->skeleton.size(); ++i)
        {
            const auto& animBone = animationData->skeleton[i];
            EvaluatedBone& eval = evaluatedBones[i];

            // Find animation channel for this bone
            auto it = boneNameToChannelIndex.find(animBone.name);
            glm::mat4 boneLocalTransform;

            if (it != boneNameToChannelIndex.end())
            {
                // Use keyframe values at time=0
                const auto& channel = animationData->channels[it->second];
                eval.position = interpolatePosition(channel, 0.0f);
                eval.rotation = interpolateRotation(channel, 0.0f);
                eval.scale = interpolateScale(channel, 0.0f);

                glm::mat4 T = glm::translate(glm::mat4(1.0f), eval.position);
                glm::mat4 R = glm::mat4_cast(eval.rotation);
                glm::mat4 S = glm::scale(glm::mat4(1.0f), eval.scale);
                boneLocalTransform = T * R * S;
            }
            else
            {
                // No animation channel - use the bone's default transform
                boneLocalTransform = animBone.offsetMatrix;
            }

            // Apply preTransform
            eval.localTransform = animBone.preTransform * boneLocalTransform;
        }

        // Compute world transforms at time=0
        computeWorldTransforms(false);

        // Now compute correction for each mesh bone using the actual t=0 world transforms
        for (size_t meshBoneIdx = 0; meshBoneIdx < meshSkeleton->boneNames.size(); ++meshBoneIdx)
        {
            int32_t animBoneIdx = meshBoneToAnimBone[meshBoneIdx];
            if (animBoneIdx < 0)
            {
                bindPoseCorrection[meshBoneIdx] = glm::mat4(1.0f);
                continue;
            }

            // Expected world transform at bind pose = inverse of inverse bind pose
            const glm::mat4& invBind = meshSkeleton->inverseBindPoses[meshBoneIdx];
            glm::mat4 expectedBindPoseWorld = glm::inverse(invBind);

            // Actual world transform from animation at time=0 (from keyframes)
            const glm::mat4& animT0World = evaluatedBones[animBoneIdx].worldTransform;

            // Correction = expected * inverse(actual)
            // When applied: correction * globalInv * animWorld * invBind
            // At t=0: correction * globalInv * animT0World * invBind
            //       = expectedWorld * inv(animT0World) * globalInv * animT0World * invBind
            // If globalInv is identity:
            //       = expectedWorld * invBind = inv(invBind) * invBind = identity
            bindPoseCorrection[meshBoneIdx] = expectedBindPoseWorld * glm::inverse(animT0World);

            // Debug: log correction for first 5 bones
            if (meshBoneIdx < 5)
            {
                const auto& corr = bindPoseCorrection[meshBoneIdx];
                bool isIdentity = true;
                for (int c = 0; c < 4 && isIdentity; ++c)
                {
                    for (int r = 0; r < 4 && isIdentity; ++r)
                    {
                        float expected = (c == r) ? 1.0f : 0.0f;
                        if (std::abs(corr[c][r] - expected) > 0.0001f) isIdentity = false;
                    }
                }
                loggerInfo("BindPoseCorrection[{}] '{}': {}",
                    meshBoneIdx, meshSkeleton->boneNames[meshBoneIdx],
                    isIdentity ? "IDENTITY (no correction needed)" : "NON-IDENTITY (correction applied)");
                if (!isIdentity)
                {
                    loggerInfo("  expectedWorld trans=({:.4f},{:.4f},{:.4f})",
                        expectedBindPoseWorld[3][0], expectedBindPoseWorld[3][1], expectedBindPoseWorld[3][2]);
                    loggerInfo("  animT0World trans=({:.4f},{:.4f},{:.4f})",
                        animT0World[3][0], animT0World[3][1], animT0World[3][2]);
                    loggerInfo("  correction trans=({:.4f},{:.4f},{:.4f})",
                        corr[3][0], corr[3][1], corr[3][2]);
                }
            }
        }
        loggerInfo("=== END BIND POSE CORRECTION ===");
    }

    void AnimationEvaluator::clear()
    {
        animationData = nullptr;
        meshSkeleton = nullptr;
        boneNameToChannelIndex.clear();
        meshBoneToAnimBone.clear();
        animBoneToMeshBone.clear();
        bindPoseCorrection.clear();
        evaluatedBones.clear();
    }

    glm::mat4 AnimationEvaluator::computeRestPoseWorldTransform(size_t animBoneIdx) const
    {
        if (!animationData || animBoneIdx >= animationData->skeleton.size())
            return glm::mat4(1.0f);

        const auto& animBone = animationData->skeleton[animBoneIdx];

        // Local transform at rest = preTransform * offsetMatrix
        glm::mat4 localTransform = animBone.preTransform * animBone.offsetMatrix;

        // Compute world transform by traversing up the parent chain
        if (animBone.parentIndex >= 0 && animBone.parentIndex < static_cast<int32_t>(animationData->skeleton.size()))
        {
            glm::mat4 parentWorld = computeRestPoseWorldTransform(static_cast<size_t>(animBone.parentIndex));
            return parentWorld * localTransform;
        }
        else
        {
            // Root bone
            return localTransform;
        }
    }

    void AnimationEvaluator::buildBoneToChannelMap()
    {
        boneNameToChannelIndex.clear();
        if (!animationData) return;

        for (size_t i = 0; i < animationData->channels.size(); ++i)
        {
            boneNameToChannelIndex[animationData->channels[i].boneName] = i;
        }
    }

    void AnimationEvaluator::buildSkeletonMapping()
    {
        meshBoneToAnimBone.clear();
        animBoneToMeshBone.clear();

        if (!animationData || !meshSkeleton) return;

        // Build animation bone name to index map
        std::unordered_map<std::string, size_t> animBoneNameToIndex;
        for (size_t i = 0; i < animationData->skeleton.size(); ++i)
        {
            animBoneNameToIndex[animationData->skeleton[i].name] = i;
        }

        // Build mesh bone name to index map
        std::unordered_map<std::string, size_t> meshBoneNameToIndex;
        for (size_t i = 0; i < meshSkeleton->boneNames.size(); ++i)
        {
            meshBoneNameToIndex[meshSkeleton->boneNames[i]] = i;
        }

        // Map mesh skeleton bones to animation skeleton bones
        meshBoneToAnimBone.resize(meshSkeleton->boneNames.size(), -1);
        for (size_t i = 0; i < meshSkeleton->boneNames.size(); ++i)
        {
            auto it = animBoneNameToIndex.find(meshSkeleton->boneNames[i]);
            if (it != animBoneNameToIndex.end())
            {
                meshBoneToAnimBone[i] = static_cast<int32_t>(it->second);
            }
        }

        // Map animation skeleton bones to mesh skeleton bones
        animBoneToMeshBone.resize(animationData->skeleton.size(), -1);
        for (size_t i = 0; i < animationData->skeleton.size(); ++i)
        {
            auto it = meshBoneNameToIndex.find(animationData->skeleton[i].name);
            if (it != meshBoneNameToIndex.end())
            {
                animBoneToMeshBone[i] = static_cast<int32_t>(it->second);
            }
        }
    }

    std::vector<glm::mat4> AnimationEvaluator::evaluatePose(float timeInTicks) const
    {
        if (!animationData || !meshSkeleton)
        {
            return {};
        }

        static int debugCounter = 0;
        bool shouldLog = (debugCounter++ % 60 == 0); // Log once per second at 60fps

        if (shouldLog)
        {
            loggerInfo("=== evaluatePose DEBUG (time={:.2f}) ===", timeInTicks);
            loggerInfo("  meshSkeleton->boneNames.size()={}", meshSkeleton->boneNames.size());
            loggerInfo("  meshSkeleton->inverseBindPoses.size()={}", meshSkeleton->inverseBindPoses.size());
            loggerInfo("  animationData->skeleton.size()={}", animationData->skeleton.size());
            loggerInfo("  evaluatedBones.size()={}", evaluatedBones.size());
            loggerInfo("  meshBoneToAnimBone.size()={}", meshBoneToAnimBone.size());
        }

        // Evaluate each bone in the animation skeleton
        for (size_t i = 0; i < animationData->skeleton.size(); ++i)
        {
            const auto& animBone = animationData->skeleton[i];
            EvaluatedBone& eval = evaluatedBones[i];

            // Find animation channel for this bone
            auto it = boneNameToChannelIndex.find(animBone.name);
            glm::mat4 boneLocalTransform;

            if (it != boneNameToChannelIndex.end())
            {
                const auto& channel = animationData->channels[it->second];
                eval.position = interpolatePosition(channel, timeInTicks);
                eval.rotation = interpolateRotation(channel, timeInTicks);
                eval.scale = interpolateScale(channel, timeInTicks);

                // Compute local transform: T * R * S
                glm::mat4 T = glm::translate(glm::mat4(1.0f), eval.position);
                glm::mat4 R = glm::mat4_cast(eval.rotation);
                glm::mat4 S = glm::scale(glm::mat4(1.0f), eval.scale);
                boneLocalTransform = T * R * S;

                // Debug: Log local transform computation for first 5 bones
                if (shouldLog && i < 5)
                {
                    loggerInfo("LOCAL[{}] '{}': pos=({:.2f},{:.2f},{:.2f}) hasChannel=YES",
                        i, animBone.name, eval.position.x, eval.position.y, eval.position.z);
                }
            }
            else
            {
                // No animation channel - use the bone's default transform
                boneLocalTransform = animBone.offsetMatrix;
                eval.position = glm::vec3(0.0f);
                eval.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                eval.scale = glm::vec3(1.0f);

                if (shouldLog && i < 5)
                {
                    loggerInfo("LOCAL[{}] '{}': localTrans=({:.2f},{:.2f},{:.2f}) hasChannel=NO",
                        i, animBone.name,
                        boneLocalTransform[3][0], boneLocalTransform[3][1], boneLocalTransform[3][2]);
                }
            }

            // Apply preTransform (accumulated non-bone parent transforms)
            eval.localTransform = animBone.preTransform * boneLocalTransform;
        }

        // Compute world transforms
        computeWorldTransforms(shouldLog);

        // Build final bone matrices for GPU
        // Result should be indexed by mesh skeleton bone index
        std::vector<glm::mat4> boneMatrices(meshSkeleton->boneNames.size(), glm::mat4(1.0f));

        for (size_t meshBoneIdx = 0; meshBoneIdx < meshSkeleton->boneNames.size(); ++meshBoneIdx)
        {
            int32_t animBoneIdx = meshBoneToAnimBone[meshBoneIdx];
            if (animBoneIdx >= 0 && animBoneIdx < static_cast<int32_t>(evaluatedBones.size()))
            {
                // Standard skinning matrix = globalInverseTransform * worldTransform * inverseBindPose
                // Try without correction first to verify the base formula works
                const glm::mat4& globalInv = animationData->globalInverseTransform;
                const glm::mat4& worldTrans = evaluatedBones[animBoneIdx].worldTransform;
                const glm::mat4& invBind = meshSkeleton->inverseBindPoses[meshBoneIdx];

                // Step-by-step computation for debugging
                glm::mat4 step1 = worldTrans * invBind;  // First multiply
                glm::mat4 step2 = globalInv * step1;     // Then apply global inverse

                boneMatrices[meshBoneIdx] = step2;

                // Debug step-by-step for first 2 bones
                if (shouldLog && meshBoneIdx < 2)
                {
                    loggerInfo("STEP-BY-STEP for bone[{}]:", meshBoneIdx);
                    loggerInfo("  animBoneIdx={}, &worldTrans={}, &invBind={}",
                        animBoneIdx, (void*)&worldTrans, (void*)&invBind);
                    loggerInfo("  worldTrans*invBind trans=({:.4f},{:.4f},{:.4f})",
                        step1[3][0], step1[3][1], step1[3][2]);
                    loggerInfo("  globalInv*(worldTrans*invBind) trans=({:.4f},{:.4f},{:.4f})",
                        step2[3][0], step2[3][1], step2[3][2]);
                }

                // Debug: trace computation for first 5 bones on first frame
                if (shouldLog && meshBoneIdx < 5)
                {
                    const auto& result = boneMatrices[meshBoneIdx];
                    loggerInfo("BONE[{}] '{}': meshIdx={} animIdx={}",
                        meshBoneIdx, meshSkeleton->boneNames[meshBoneIdx],
                        meshBoneIdx, animBoneIdx);
                    loggerInfo("  worldTrans trans=({:.4f},{:.4f},{:.4f})",
                        worldTrans[3][0], worldTrans[3][1], worldTrans[3][2]);
                    loggerInfo("  invBind trans=({:.4f},{:.4f},{:.4f})",
                        invBind[3][0], invBind[3][1], invBind[3][2]);
                    loggerInfo("  RESULT trans=({:.4f},{:.4f},{:.4f})",
                        result[3][0], result[3][1], result[3][2]);
                }
            }
            else
            {
                // Bone not animated - use identity (no transformation)
                boneMatrices[meshBoneIdx] = glm::mat4(1.0f);
            }
        }

        // Summary: compare first two bone matrices and dump full matrices
        if (shouldLog && boneMatrices.size() >= 2)
        {
            loggerInfo("=== SUMMARY: Comparing boneMatrices[0] vs [1] ===");

            // Dump full matrix 0
            loggerInfo("Matrix[0] full:");
            for (int row = 0; row < 4; ++row)
            {
                loggerInfo("  row{}: ({:.6f}, {:.6f}, {:.6f}, {:.6f})",
                    row, boneMatrices[0][0][row], boneMatrices[0][1][row],
                    boneMatrices[0][2][row], boneMatrices[0][3][row]);
            }

            // Dump full matrix 1
            loggerInfo("Matrix[1] full:");
            for (int row = 0; row < 4; ++row)
            {
                loggerInfo("  row{}: ({:.6f}, {:.6f}, {:.6f}, {:.6f})",
                    row, boneMatrices[1][0][row], boneMatrices[1][1][row],
                    boneMatrices[1][2][row], boneMatrices[1][3][row]);
            }

            bool identical = true;
            float maxDiff = 0.0f;
            for (int col = 0; col < 4 && identical; ++col)
            {
                for (int row = 0; row < 4 && identical; ++row)
                {
                    float diff = std::abs(boneMatrices[0][col][row] - boneMatrices[1][col][row]);
                    maxDiff = std::max(maxDiff, diff);
                    if (diff > 0.0001f)
                    {
                        identical = false;
                    }
                }
            }
            loggerInfo("  Max difference: {:.6f}", maxDiff);
            loggerInfo("  Matrices are: {}", identical ? "IDENTICAL (BUG!)" : "DIFFERENT (good)");
        }

        // REST POSE IDENTITY VERIFICATION
        // At time=0, worldTransform * inverseBindPose should be close to identity
        // This is a critical diagnostic check
        static bool restPoseChecked = false;
        if (!restPoseChecked && timeInTicks < 1.0f)
        {
            restPoseChecked = true;
            loggerInfo("=== REST POSE IDENTITY VERIFICATION (time={:.2f}) ===", timeInTicks);
            size_t identityCount = 0;
            size_t nonIdentityCount = 0;

            for (size_t meshBoneIdx = 0; meshBoneIdx < meshSkeleton->boneNames.size(); ++meshBoneIdx)
            {
                int32_t animBoneIdx = meshBoneToAnimBone[meshBoneIdx];
                if (animBoneIdx < 0) continue;

                const glm::mat4& result = boneMatrices[meshBoneIdx];

                // Check if result is close to identity
                bool isNearIdentity = true;
                float maxDeviation = 0.0f;
                for (int c = 0; c < 4; ++c)
                {
                    for (int r = 0; r < 4; ++r)
                    {
                        float expected = (c == r) ? 1.0f : 0.0f;
                        float deviation = std::abs(result[c][r] - expected);
                        maxDeviation = std::max(maxDeviation, deviation);
                        if (deviation > 0.1f)
                        {
                            isNearIdentity = false;
                        }
                    }
                }

                if (isNearIdentity)
                {
                    ++identityCount;
                }
                else
                {
                    ++nonIdentityCount;
                    // Log details for non-identity matrices
                    loggerWarning("REST POSE: Bone[{}] '{}' NOT identity! maxDeviation={:.4f}",
                        meshBoneIdx, meshSkeleton->boneNames[meshBoneIdx], maxDeviation);
                    loggerWarning("  Result matrix diag=({:.4f},{:.4f},{:.4f},{:.4f})",
                        result[0][0], result[1][1], result[2][2], result[3][3]);
                    loggerWarning("  Result matrix trans=({:.4f},{:.4f},{:.4f})",
                        result[3][0], result[3][1], result[3][2]);

                    // Also log the world transform and inverse bind pose for this bone
                    const glm::mat4& worldTrans = evaluatedBones[animBoneIdx].worldTransform;
                    const glm::mat4& invBind = meshSkeleton->inverseBindPoses[meshBoneIdx];
                    loggerWarning("  WorldTrans diag=({:.4f},{:.4f},{:.4f},{:.4f}) trans=({:.4f},{:.4f},{:.4f})",
                        worldTrans[0][0], worldTrans[1][1], worldTrans[2][2], worldTrans[3][3],
                        worldTrans[3][0], worldTrans[3][1], worldTrans[3][2]);
                    loggerWarning("  InvBind diag=({:.4f},{:.4f},{:.4f},{:.4f}) trans=({:.4f},{:.4f},{:.4f})",
                        invBind[0][0], invBind[1][1], invBind[2][2], invBind[3][3],
                        invBind[3][0], invBind[3][1], invBind[3][2]);
                }
            }

            loggerInfo("REST POSE SUMMARY: {} bones near identity, {} bones NOT identity",
                identityCount, nonIdentityCount);
            if (nonIdentityCount > 0)
            {
                loggerWarning("REST POSE ISSUE DETECTED: {} bones have incorrect bind pose!",
                    nonIdentityCount);
                loggerWarning("This suggests: missing global inverse transform OR incorrect preTransform accumulation");
            }
            loggerInfo("=== END REST POSE VERIFICATION ===");
        }

        return boneMatrices;
    }

    void AnimationEvaluator::computeWorldTransforms(bool shouldLog) const
    {
        if (!animationData) return;

        for (size_t i = 0; i < animationData->skeleton.size(); ++i)
        {
            const auto& animBone = animationData->skeleton[i];
            EvaluatedBone& eval = evaluatedBones[i];

            if (animBone.parentIndex >= 0 &&
                animBone.parentIndex < static_cast<int32_t>(evaluatedBones.size()))
            {
                eval.worldTransform =
                    evaluatedBones[animBone.parentIndex].worldTransform * eval.localTransform;
            }
            else
            {
                // Root bone
                eval.worldTransform = eval.localTransform;
            }

            // Debug: Print world transforms for first 5 bones
            if (shouldLog && i < 5)
            {
                loggerInfo("WORLD animBone[{}] '{}': worldTrans col3=({:.3f}, {:.3f}, {:.3f})",
                    i, animBone.name,
                    eval.worldTransform[3][0], eval.worldTransform[3][1], eval.worldTransform[3][2]);
            }
        }
    }

    glm::vec3 AnimationEvaluator::interpolatePosition(const resource::BoneAnimation& channel,
                                                       float time) const
    {
        if (channel.positionKeys.empty())
            return glm::vec3(0.0f);

        if (channel.positionKeys.size() == 1)
            return channel.positionKeys[0].position;

        // Find the two keyframes surrounding the current time
        size_t i = 0;
        for (; i < channel.positionKeys.size() - 1; ++i)
        {
            if (time < channel.positionKeys[i + 1].time)
                break;
        }

        if (i >= channel.positionKeys.size() - 1)
            return channel.positionKeys.back().position;

        const auto& k0 = channel.positionKeys[i];
        const auto& k1 = channel.positionKeys[i + 1];

        float dt = k1.time - k0.time;
        float t = (dt > 0.0f) ? (time - k0.time) / dt : 0.0f;
        t = glm::clamp(t, 0.0f, 1.0f);

        return glm::mix(k0.position, k1.position, t);
    }

    glm::quat AnimationEvaluator::interpolateRotation(const resource::BoneAnimation& channel,
                                                       float time) const
    {
        if (channel.rotationKeys.empty())
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

        if (channel.rotationKeys.size() == 1)
            return channel.rotationKeys[0].rotation;

        size_t i = 0;
        for (; i < channel.rotationKeys.size() - 1; ++i)
        {
            if (time < channel.rotationKeys[i + 1].time)
                break;
        }

        if (i >= channel.rotationKeys.size() - 1)
            return channel.rotationKeys.back().rotation;

        const auto& k0 = channel.rotationKeys[i];
        const auto& k1 = channel.rotationKeys[i + 1];

        float dt = k1.time - k0.time;
        float t = (dt > 0.0f) ? (time - k0.time) / dt : 0.0f;
        t = glm::clamp(t, 0.0f, 1.0f);

        return glm::slerp(k0.rotation, k1.rotation, t);
    }

    glm::vec3 AnimationEvaluator::interpolateScale(const resource::BoneAnimation& channel,
                                                    float time) const
    {
        if (channel.scalingKeys.empty())
            return glm::vec3(1.0f);

        if (channel.scalingKeys.size() == 1)
            return channel.scalingKeys[0].scale;

        size_t i = 0;
        for (; i < channel.scalingKeys.size() - 1; ++i)
        {
            if (time < channel.scalingKeys[i + 1].time)
                break;
        }

        if (i >= channel.scalingKeys.size() - 1)
            return channel.scalingKeys.back().scale;

        const auto& k0 = channel.scalingKeys[i];
        const auto& k1 = channel.scalingKeys[i + 1];

        float dt = k1.time - k0.time;
        float t = (dt > 0.0f) ? (time - k0.time) / dt : 0.0f;
        t = glm::clamp(t, 0.0f, 1.0f);

        return glm::mix(k0.scale, k1.scale, t);
    }

    float AnimationEvaluator::getDurationSeconds() const
    {
        if (!animationData) return 0.0f;
        float tps = animationData->ticksPerSecond > 0.0f ? animationData->ticksPerSecond : 24.0f;
        return animationData->duration / tps;
    }

    float AnimationEvaluator::secondsToTicks(float seconds) const
    {
        if (!animationData) return 0.0f;
        float tps = animationData->ticksPerSecond > 0.0f ? animationData->ticksPerSecond : 24.0f;
        return seconds * tps;
    }

    float AnimationEvaluator::ticksToSeconds(float ticks) const
    {
        if (!animationData) return 0.0f;
        float tps = animationData->ticksPerSecond > 0.0f ? animationData->ticksPerSecond : 24.0f;
        return ticks / tps;
    }
}

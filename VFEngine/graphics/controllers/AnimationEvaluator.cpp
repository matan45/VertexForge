#include "AnimationEvaluator.hpp"
#include "print/Logger.hpp"
#include <algorithm>
#include <functional>

namespace controllers
{
    void AnimationEvaluator::loadAnimation(const resource::AnimationData& animation)
    {
        clear();

        animationData = &animation;

        if (!animationData->hasInverseBindPoses())
        {
            loggerError("Animation '{}' does not have inverse bind poses - cannot play",
                       animationData->name);
            return;
        }

        // Build bone to channel map for fast keyframe lookup
        buildBoneToChannelMap();

        // Initialize evaluated bones
        evaluatedBones.resize(animationData->skeleton.size());

        // Precompute bind poses from inverse bind poses
        // This is needed for bones without animation channels
        computedBindPoses.resize(animationData->skeleton.size());
        computedLocalBindPoses.resize(animationData->skeleton.size());

        for (size_t i = 0; i < animationData->skeleton.size(); ++i)
        {
            // Bind pose = inverse of inverse bind pose
            computedBindPoses[i] = glm::inverse(animationData->inverseBindPoses[i]);
        }

        // Compute local bind poses (relative to parent)
        for (size_t i = 0; i < animationData->skeleton.size(); ++i)
        {
            const auto& bone = animationData->skeleton[i];
            if (bone.parentIndex >= 0 && bone.parentIndex < static_cast<int32_t>(computedBindPoses.size()))
            {
                // Local bind pose = inverse(parentBindPose) * childBindPose
                computedLocalBindPoses[i] = glm::inverse(computedBindPoses[bone.parentIndex]) * computedBindPoses[i];
            }
            else
            {
                // Root bone - local bind pose is world bind pose
                computedLocalBindPoses[i] = computedBindPoses[i];
            }
        }

        loggerInfo("Loaded animation '{}': {} bones, {} channels, {} inverse bind poses",
                  animationData->name,
                  animationData->skeleton.size(),
                  animationData->channels.size(),
                  animationData->inverseBindPoses.size());

        // Debug: log first few bones to verify data
        for (size_t i = 0; i < std::min(size_t(5), animationData->skeleton.size()); ++i)
        {
            const auto& bone = animationData->skeleton[i];
            const auto& invBind = animationData->inverseBindPoses[i];
            const auto& localBind = computedLocalBindPoses[i];
            loggerInfo("  Bone[{}] '{}': parent={}, invBind[3]=({:.3f}, {:.3f}, {:.3f}), localBind[3]=({:.3f}, {:.3f}, {:.3f})",
                i, bone.name, bone.parentIndex,
                invBind[3][0], invBind[3][1], invBind[3][2],
                localBind[3][0], localBind[3][1], localBind[3][2]);
        }

        // Debug: log global inverse transform
        const auto& gi = animationData->globalInverseTransform;
        loggerInfo("  GlobalInverse[3]=({:.3f}, {:.3f}, {:.3f})",
            gi[3][0], gi[3][1], gi[3][2]);
    }

    void AnimationEvaluator::clear()
    {
        animationData = nullptr;
        boneNameToChannelIndex.clear();
        evaluatedBones.clear();
        computedBindPoses.clear();
        computedLocalBindPoses.clear();
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

    std::vector<glm::mat4> AnimationEvaluator::evaluatePose(float timeInTicks) const
    {
        if (!animationData || animationData->skeleton.empty() || !animationData->hasInverseBindPoses())
        {
            return {};
        }

        // Wrap time to animation duration
        float duration = animationData->duration;
        if (duration > 0.0f)
        {
            timeInTicks = std::fmod(timeInTicks, duration);
            if (timeInTicks < 0.0f) timeInTicks += duration;
        }

        // Evaluate local transforms for each bone
        for (size_t i = 0; i < animationData->skeleton.size(); ++i)
        {
            const auto& animBone = animationData->skeleton[i];
            EvaluatedBone& eval = evaluatedBones[i];

            // Find animation channel for this bone
            auto it = boneNameToChannelIndex.find(animBone.name);

            glm::mat4 boneLocalTransform;
            if (it != boneNameToChannelIndex.end())
            {
                // Animated bone: interpolate keyframes
                const auto& channel = animationData->channels[it->second];

                glm::vec3 pos = interpolatePosition(channel, timeInTicks);
                glm::quat rot = interpolateRotation(channel, timeInTicks);
                glm::vec3 scl = interpolateScale(channel, timeInTicks);

                // CRITICAL FIX: If no position/rotation/scale keyframes exist,
                // use the computed local bind pose (from inverse bind poses) instead of (0,0,0)
                if (channel.positionKeys.empty())
                {
                    pos = glm::vec3(computedLocalBindPoses[i][3]);
                }
                if (channel.rotationKeys.empty())
                {
                    rot = glm::quat_cast(glm::mat3(computedLocalBindPoses[i]));
                }
                if (channel.scalingKeys.empty())
                {
                    // Extract scale from local bind pose matrix
                    scl = glm::vec3(
                        glm::length(glm::vec3(computedLocalBindPoses[i][0])),
                        glm::length(glm::vec3(computedLocalBindPoses[i][1])),
                        glm::length(glm::vec3(computedLocalBindPoses[i][2]))
                    );
                }

                eval.position = pos;
                eval.rotation = rot;
                eval.scale = scl;

                glm::mat4 T = glm::translate(glm::mat4(1.0f), pos);
                glm::mat4 R = glm::mat4_cast(rot);
                glm::mat4 S = glm::scale(glm::mat4(1.0f), scl);
                boneLocalTransform = T * R * S;

                // DEBUG: Log first frame for first few bones
                static bool loggedOnce = false;
                if (!loggedOnce && i < 5)
                {
                    glm::vec3 eulerDeg = glm::degrees(glm::eulerAngles(rot));
                    loggerInfo("Bone[{}] '{}': pos=({:.2f},{:.2f},{:.2f}) rot=({:.1f},{:.1f},{:.1f})deg posKeys={} rotKeys={} usedRestPos={}",
                        i, animBone.name,
                        pos.x, pos.y, pos.z,
                        eulerDeg.x, eulerDeg.y, eulerDeg.z,
                        channel.positionKeys.size(), channel.rotationKeys.size(),
                        channel.positionKeys.empty() ? "YES" : "no");

                    // Also log first position keyframe if exists
                    if (!channel.positionKeys.empty())
                    {
                        const auto& firstPosKey = channel.positionKeys[0];
                        loggerInfo("    First posKey: time={:.2f} pos=({:.2f},{:.2f},{:.2f})",
                            firstPosKey.time, firstPosKey.position.x, firstPosKey.position.y, firstPosKey.position.z);
                    }
                    // Log computed bind pose for comparison
                    loggerInfo("    computedLocalBindPose[3]: ({:.2f},{:.2f},{:.2f})",
                        computedLocalBindPoses[i][3][0], computedLocalBindPoses[i][3][1], computedLocalBindPoses[i][3][2]);

                    if (i == 4) loggedOnce = true;
                }
            }
            else
            {
                // Non-animated bone: use computed local bind pose from inverse bind poses
                // This is more reliable than offsetMatrix which may be (0,0,0)
                boneLocalTransform = computedLocalBindPoses[i];
                eval.position = glm::vec3(computedLocalBindPoses[i][3]);
                eval.rotation = glm::quat_cast(glm::mat3(computedLocalBindPoses[i]));
                eval.scale = glm::vec3(1.0f);

                // DEBUG: Log once for bones without channels
                static bool loggedOnceRest = false;
                static int noChannelCount = 0;
                if (!loggedOnceRest)
                {
                    loggerInfo("Bone[{}] '{}' NO CHANNEL: using localBindPose[3]=({:.2f},{:.2f},{:.2f})",
                        i, animBone.name,
                        computedLocalBindPoses[i][3][0], computedLocalBindPoses[i][3][1], computedLocalBindPoses[i][3][2]);
                    noChannelCount++;
                    if (noChannelCount >= 3) loggedOnceRest = true;
                }
            }

            eval.localTransform = boneLocalTransform;
        }

        // Compute world transforms
        computeWorldTransforms(false);

        // Build final bone matrices: globalInverseTransform * worldTransform * inverseBindPose
        // - worldTransform: bone's animated position in scene/world space (includes root transforms)
        // - inverseBindPose: transforms mesh vertices to bone-local space
        // - globalInverseTransform: converts result back to mesh/model space
        std::vector<glm::mat4> boneMatrices(animationData->skeleton.size(), glm::mat4(1.0f));

        // Debug: log once per second
        static float lastLogTime = -10.0f;
        bool shouldDebugLog = (timeInTicks - lastLogTime) > animationData->ticksPerSecond;
        if (shouldDebugLog)
        {
            lastLogTime = timeInTicks;
            loggerInfo("=== Evaluating pose at time {:.2f} ticks ===", timeInTicks);

            // DEBUG: Verify world transforms are unique
            for (size_t i = 0; i < std::min(size_t(3), evaluatedBones.size()); ++i)
            {
                const auto& e = evaluatedBones[i];
                loggerInfo("  Bone[{}]: local[3]=({:.2f},{:.2f},{:.2f}), world[3]=({:.2f},{:.2f},{:.2f})",
                    i,
                    e.localTransform[3][0], e.localTransform[3][1], e.localTransform[3][2],
                    e.worldTransform[3][0], e.worldTransform[3][1], e.worldTransform[3][2]);
            }

            // Debug: show globalInverseTransform
            const auto& gi = animationData->globalInverseTransform;
            loggerInfo("  globalInverse diagonal=({:.4f},{:.4f},{:.4f},{:.4f})",
                gi[0][0], gi[1][1], gi[2][2], gi[3][3]);
            loggerInfo("  globalInverse[3]=({:.4f},{:.4f},{:.4f},{:.4f})",
                gi[3][0], gi[3][1], gi[3][2], gi[3][3]);

            // Debug: show preTransform and local transform for root bone
            if (!animationData->skeleton.empty())
            {
                const auto& rootBone = animationData->skeleton[0];
                const auto& rootEval = evaluatedBones[0];
                loggerInfo("  Root preTransform[3]=({:.2f},{:.2f},{:.2f})",
                    rootBone.preTransform[3][0], rootBone.preTransform[3][1], rootBone.preTransform[3][2]);
                loggerInfo("  Root localTransform[3]=({:.2f},{:.2f},{:.2f})",
                    rootEval.localTransform[3][0], rootEval.localTransform[3][1], rootEval.localTransform[3][2]);
                loggerInfo("  Root offsetMatrix[3]=({:.2f},{:.2f},{:.2f})",
                    rootBone.offsetMatrix[3][0], rootBone.offsetMatrix[3][1], rootBone.offsetMatrix[3][2]);
            }
        }

        for (size_t i = 0; i < animationData->skeleton.size(); ++i)
        {
            const glm::mat4& worldTrans = evaluatedBones[i].worldTransform;
            const glm::mat4& invBind = animationData->inverseBindPoses[i];

            // Final bone matrix for GPU skinning
            boneMatrices[i] = animationData->globalInverseTransform * worldTrans * invBind;

            // Store bone position for debug visualization
            // For a vertex at the bone's bind pose origin (weighted 100% to this bone):
            // finalPos = globalInverse * worldTransform * invBind * bindPose
            //          = globalInverse * worldTransform * identity (since invBind * bindPose = identity)
            //          = globalInverse * worldTransform
            glm::vec4 boneVisualPos = animationData->globalInverseTransform * glm::vec4(glm::vec3(worldTrans[3]), 1.0f);
            evaluatedBones[i].skinnedPosition = glm::vec3(boneVisualPos);

            // Debug: log first few bones
            if (shouldDebugLog && i < 3)
            {
                // Also compute without globalInverse to compare
                glm::mat4 testMatrix = worldTrans * invBind;
                loggerInfo("  Bone[{}] '{}': world[3]=({:.2f},{:.2f},{:.2f})",
                    i, animationData->skeleton[i].name,
                    worldTrans[3][0], worldTrans[3][1], worldTrans[3][2]);
                loggerInfo("    world diag=({:.2f},{:.2f},{:.2f},{:.2f})",
                    worldTrans[0][0], worldTrans[1][1], worldTrans[2][2], worldTrans[3][3]);
                loggerInfo("    invBind[3]=({:.2f},{:.2f},{:.2f})",
                    invBind[3][0], invBind[3][1], invBind[3][2]);
                loggerInfo("    invBind diag=({:.2f},{:.2f},{:.2f},{:.2f})",
                    invBind[0][0], invBind[1][1], invBind[2][2], invBind[3][3]);
                loggerInfo("    world*invBind[3]=({:.2f},{:.2f},{:.2f})",
                    testMatrix[3][0], testMatrix[3][1], testMatrix[3][2]);
                loggerInfo("    final diag=({:.2f},{:.2f},{:.2f},{:.2f})",
                    boneMatrices[i][0][0], boneMatrices[i][1][1], boneMatrices[i][2][2], boneMatrices[i][3][3]);
            }
        }

        return boneMatrices;
    }

    void AnimationEvaluator::computeWorldTransforms(bool shouldLog) const
    {
        if (!animationData) return;

        // HIERARCHY-BASED APPROACH: Properly propagate parent transforms to children
        // This ensures that even non-animated bones move when their parents are animated.
        //
        // For each bone:
        // - Root bones (parentIndex < 0): worldTransform = localTransform
        // - Child bones: worldTransform = parent.worldTransform * localTransform
        //
        // This assumes bones are ordered parent-before-child (which is standard).

        for (size_t i = 0; i < animationData->skeleton.size(); ++i)
        {
            const auto& bone = animationData->skeleton[i];
            EvaluatedBone& eval = evaluatedBones[i];

            if (bone.parentIndex >= 0 && bone.parentIndex < static_cast<int32_t>(evaluatedBones.size()))
            {
                // Child bone: inherit parent's world transform
                const EvaluatedBone& parentEval = evaluatedBones[bone.parentIndex];
                eval.worldTransform = parentEval.worldTransform * eval.localTransform;
            }
            else
            {
                // Root bone: local transform IS world transform
                eval.worldTransform = eval.localTransform;
            }

            if (shouldLog && i < 5)
            {
                const glm::mat4& bindPose = computedBindPoses[i];
                loggerInfo("WORLD bone[{}] '{}': parent={}, local[3]=({:.2f},{:.2f},{:.2f}), world[3]=({:.2f},{:.2f},{:.2f})",
                    i, bone.name, bone.parentIndex,
                    eval.localTransform[3][0], eval.localTransform[3][1], eval.localTransform[3][2],
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

        // Find the two keyframes to interpolate between
        size_t index0 = 0;
        for (size_t i = 0; i < channel.positionKeys.size() - 1; ++i)
        {
            if (time < channel.positionKeys[i + 1].time)
            {
                index0 = i;
                break;
            }
            index0 = i;
        }

        size_t index1 = (index0 + 1) % channel.positionKeys.size();

        float t0 = channel.positionKeys[index0].time;
        float t1 = channel.positionKeys[index1].time;

        float factor = (t1 != t0) ? glm::clamp((time - t0) / (t1 - t0), 0.0f, 1.0f) : 0.0f;

        return glm::mix(channel.positionKeys[index0].position,
                       channel.positionKeys[index1].position,
                       factor);
    }

    glm::quat AnimationEvaluator::interpolateRotation(const resource::BoneAnimation& channel,
                                                       float time) const
    {
        if (channel.rotationKeys.empty())
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

        if (channel.rotationKeys.size() == 1)
            return channel.rotationKeys[0].rotation;

        // Find the two keyframes to interpolate between
        size_t index0 = 0;
        for (size_t i = 0; i < channel.rotationKeys.size() - 1; ++i)
        {
            if (time < channel.rotationKeys[i + 1].time)
            {
                index0 = i;
                break;
            }
            index0 = i;
        }

        size_t index1 = (index0 + 1) % channel.rotationKeys.size();

        float t0 = channel.rotationKeys[index0].time;
        float t1 = channel.rotationKeys[index1].time;

        float factor = (t1 != t0) ? glm::clamp((time - t0) / (t1 - t0), 0.0f, 1.0f) : 0.0f;

        return glm::slerp(channel.rotationKeys[index0].rotation,
                         channel.rotationKeys[index1].rotation,
                         factor);
    }

    glm::vec3 AnimationEvaluator::interpolateScale(const resource::BoneAnimation& channel,
                                                    float time) const
    {
        if (channel.scalingKeys.empty())
            return glm::vec3(1.0f);

        if (channel.scalingKeys.size() == 1)
            return channel.scalingKeys[0].scale;

        // Find the two keyframes to interpolate between
        size_t index0 = 0;
        for (size_t i = 0; i < channel.scalingKeys.size() - 1; ++i)
        {
            if (time < channel.scalingKeys[i + 1].time)
            {
                index0 = i;
                break;
            }
            index0 = i;
        }

        size_t index1 = (index0 + 1) % channel.scalingKeys.size();

        float t0 = channel.scalingKeys[index0].time;
        float t1 = channel.scalingKeys[index1].time;

        float factor = (t1 != t0) ? glm::clamp((time - t0) / (t1 - t0), 0.0f, 1.0f) : 0.0f;

        return glm::mix(channel.scalingKeys[index0].scale,
                       channel.scalingKeys[index1].scale,
                       factor);
    }
}

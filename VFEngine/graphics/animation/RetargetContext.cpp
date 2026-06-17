#include "RetargetContext.hpp"

#include <glm/gtc/quaternion.hpp>
#include <optional>
#include <unordered_map>

namespace animation
{
    using retargeting::HumanoidBoneRole;

    namespace
    {
        // Model-space bind position of a bone (prefer bindPoses; fall back to the
        // inverse of inverseBindPoses, which the evaluator guarantees exists).
        std::optional<glm::vec3> modelBindPos(const resource::SkeletonData& skel, int index)
        {
            if (index < 0) return std::nullopt;
            const auto idx = static_cast<size_t>(index);
            if (idx < skel.bindPoses.size())
                return glm::vec3(skel.bindPoses[idx][3]);
            if (idx < skel.inverseBindPoses.size())
                return glm::vec3(glm::inverse(skel.inverseBindPoses[idx])[3]);
            return std::nullopt;
        }

        float legLength(const resource::SkeletonData& skel, const retargeting::HumanoidRigData& rig)
        {
            auto pos = [&](HumanoidBoneRole role) -> std::optional<glm::vec3> {
                const auto* b = rig.find(role);
                if (!b) return std::nullopt;
                return modelBindPos(skel, skel.getBoneIndex(b->boneName));
            };
            const auto up = pos(HumanoidBoneRole::LeftUpperLeg);
            const auto lo = pos(HumanoidBoneRole::LeftLowerLeg);
            const auto ft = pos(HumanoidBoneRole::LeftFoot);
            if (up && lo && ft)
                return glm::length(*lo - *up) + glm::length(*ft - *lo);
            return 0.0f;
        }
    }

    RetargetContext RetargetContext::build(
        const resource::SkeletonData& targetSkeleton,
        const resource::SkeletonData* sourceSkeleton,
        const retargeting::HumanoidRigData& sourceRig,
        const retargeting::HumanoidRigData& targetRig,
        const retargeting::RetargetMapData& map)
    {
        RetargetContext ctx;
        const size_t boneCount = targetSkeleton.bones.size();
        ctx.perTargetBone.assign(boneCount, RetargetBone{});

        // Fast target-bone-name -> binding lookup.
        std::unordered_map<std::string, const retargeting::HumanoidBoneBinding*> targetByName;
        targetByName.reserve(targetRig.bindings.size());
        for (const auto& b : targetRig.bindings)
            targetByName[b.boneName] = &b;

        for (size_t t = 0; t < boneCount; ++t)
        {
            RetargetBone rb; // default: unmapped -> target bind

            const auto itT = targetByName.find(targetSkeleton.bones[t].name);
            if (itT == targetByName.end())
            {
                ctx.perTargetBone[t] = rb;
                continue;
            }

            const HumanoidBoneRole role = itT->second->role;

            if (const auto* ov = map.findOverride(role); ov && !ov->enabled)
            {
                ctx.perTargetBone[t] = rb; // role disabled -> hold bind
                continue;
            }

            const auto* srcBinding = sourceRig.find(role);
            if (!srcBinding)
            {
                ctx.perTargetBone[t] = rb; // source has no bone for this role -> hold bind
                continue;
            }

            const glm::quat qs = glm::normalize(srcBinding->referenceLocalRotation);
            const glm::quat qt = glm::normalize(itT->second->referenceLocalRotation);

            rb.mapped = true;
            rb.sourceBoneName = srcBinding->boneName;
            rb.correction = glm::normalize(qt * glm::inverse(qs));
            rb.targetRefRotation = qt;
            rb.isRoot = (role == HumanoidBoneRole::Hips);

            if (rb.isRoot)
            {
                rb.tgtHipBindLocal = glm::vec3(targetSkeleton.bones[t].offsetMatrix[3]);
                rb.srcHipBindLocal = rb.tgtHipBindLocal; // fallback if source skeleton absent
                if (sourceSkeleton)
                {
                    const int si = sourceSkeleton->getBoneIndex(srcBinding->boneName);
                    if (si >= 0)
                        rb.srcHipBindLocal = glm::vec3(sourceSkeleton->bones[static_cast<size_t>(si)].offsetMatrix[3]);
                }
            }

            ctx.perTargetBone[t] = std::move(rb);
        }

        ctx.legLengthRatio = 1.0f;
        if (sourceSkeleton)
        {
            const float srcLen = legLength(*sourceSkeleton, sourceRig);
            const float dstLen = legLength(targetSkeleton, targetRig);
            if (srcLen > 1e-5f && dstLen > 1e-5f)
                ctx.legLengthRatio = dstLen / srcLen;
        }
        return ctx;
    }
}

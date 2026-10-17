#include "RetargetContext.hpp"
#include "retargeting/RetargetBoneMapping.hpp"

#include <glm/gtc/quaternion.hpp>
#include <optional>

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
            auto sideLength = [&](HumanoidBoneRole up, HumanoidBoneRole lo, HumanoidBoneRole ft) -> float {
                const auto u = pos(up);
                const auto l = pos(lo);
                const auto f = pos(ft);
                if (u && l && f)
                    return glm::length(*l - *u) + glm::length(*f - *l);
                return 0.0f;
            };
            // Prefer the left leg; fall back to the right so an asymmetric mapping
            // (only one leg resolved) still yields a real hip-height scale.
            float len = sideLength(HumanoidBoneRole::LeftUpperLeg, HumanoidBoneRole::LeftLowerLeg,
                                   HumanoidBoneRole::LeftFoot);
            if (len <= 0.0f)
                len = sideLength(HumanoidBoneRole::RightUpperLeg, HumanoidBoneRole::RightLowerLeg,
                                 HumanoidBoneRole::RightFoot);
            return len;
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

        const retargeting::RetargetBoneMapping mapping(sourceRig, targetRig, map);

        for (size_t t = 0; t < boneCount; ++t)
        {
            RetargetBone rb; // default: unmapped -> target bind

            // No target binding, role disabled by an override, or no source bone for the
            // role -> hold bind.
            const auto match = mapping.match(targetSkeleton.bones[t].name);
            if (!match)
            {
                ctx.perTargetBone[t] = rb;
                continue;
            }

            const HumanoidBoneRole role = match->target->role;
            const auto* srcBinding = match->source;

            const glm::quat qs = glm::normalize(srcBinding->referenceLocalRotation);
            const glm::quat qt = glm::normalize(match->target->referenceLocalRotation);

            rb.mapped = true;
            rb.sourceBoneName = srcBinding->boneName;
            rb.correction = glm::normalize(qt * glm::inverse(qs));
            rb.isRoot = (role == HumanoidBoneRole::Hips);

            // Hips always retarget translation; other roles opt in via the binding
            // flag or a per-role override (VK-910).
            bool wantsTranslation = rb.isRoot || match->target->retargetTranslation;
            if (const auto* ov = map.findOverride(role); ov && ov->overrideTranslation)
                wantsTranslation = true;
            rb.retargetTranslation = wantsTranslation;

            if (rb.retargetTranslation)
            {
                rb.tgtBindLocal = glm::vec3(targetSkeleton.bones[t].offsetMatrix[3]);
                rb.srcBindLocal = rb.tgtBindLocal; // fallback if source skeleton absent
                if (sourceSkeleton)
                {
                    const int si = sourceSkeleton->getBoneIndex(srcBinding->boneName);
                    if (si >= 0)
                        rb.srcBindLocal = glm::vec3(sourceSkeleton->bones[static_cast<size_t>(si)].offsetMatrix[3]);
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

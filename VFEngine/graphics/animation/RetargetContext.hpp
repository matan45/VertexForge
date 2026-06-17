#pragma once

#include "AnimationExport.hpp"
#include "resource/Types.hpp"
#include "retargeting/RetargetTypes.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

namespace animation
{
    // Per target-skeleton bone, the precomputed data needed to drive it from a
    // source-skeleton animation channel (VK-910). Built once per (source skeleton,
    // target skeleton, rig pair); clip-channel resolution happens per clip at
    // runtime via the source bone name, so this is clip-independent.
    struct RetargetBone
    {
        bool mapped = false;                       // false -> target bone holds its bind pose
        std::string sourceBoneName;                // source channel key (looked up per clip)

        // C_t = q_t_ref * inverse(q_s_ref); maps source-rest-relative local
        // rotation into the target bone's local frame.
        glm::quat correction{1.0f, 0.0f, 0.0f, 0.0f};

        bool isRoot = false;                       // Hips: drives root-motion extraction
        // Retarget this bone's local translation (scaled by legLengthRatio). Always
        // true for Hips; enabled for other roles by HumanoidBoneBinding::retargetTranslation
        // or RetargetRoleOverride::overrideTranslation. When false the bone keeps its
        // own bind translation (rotation-only), preserving target proportions.
        bool retargetTranslation = false;
        glm::vec3 srcBindLocal{0.0f};              // source bone local bind translation
        glm::vec3 tgtBindLocal{0.0f};              // target bone local bind translation
    };

#pragma warning(push)
#pragma warning(disable: 4251)
    struct VF_ANIMATION_API RetargetContext
    {
        std::vector<RetargetBone> perTargetBone;   // indexed by target skeleton bone index
        float legLengthRatio = 1.0f;               // targetLegLength / sourceLegLength (hip-height scale)

        bool empty() const { return perTargetBone.empty(); }

        // Build the mapping. sourceSkeleton may be null (degrades: ratio = 1, hip
        // translation passes through unscaled relative to the target bind).
        static RetargetContext build(
            const resource::SkeletonData& targetSkeleton,
            const resource::SkeletonData* sourceSkeleton,
            const retargeting::HumanoidRigData& sourceRig,
            const retargeting::HumanoidRigData& targetRig,
            const retargeting::RetargetMapData& map);
    };
#pragma warning(pop)
}

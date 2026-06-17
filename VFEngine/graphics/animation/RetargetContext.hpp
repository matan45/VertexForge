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
        glm::quat targetRefRotation{1.0f, 0.0f, 0.0f, 0.0f}; // rest fallback when no rotation keys

        bool isRoot = false;                       // Hips: retarget translation + drive root motion
        glm::vec3 srcHipBindLocal{0.0f};
        glm::vec3 tgtHipBindLocal{0.0f};
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

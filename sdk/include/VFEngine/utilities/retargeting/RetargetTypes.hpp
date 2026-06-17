#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <cstdint>
#include "HumanoidBoneRole.hpp"

// Plain value types for the two retargeting assets (VK-910):
//   HumanoidRigData  (.vfrig)      — per-skeleton bone -> HumanoidBoneRole profile + T-pose reference.
//   RetargetMapData  (.vfretarget) — a source-rig -> target-rig binding with per-role overrides.
// Header-only, no engine dependencies beyond glm — included by Utilities, the
// Animation DLL (RetargetContext), the Editor, and Tests alike.
namespace retargeting
{
    // One humanoid role bound to a concrete bone in the owning skeleton.
    struct HumanoidBoneBinding
    {
        HumanoidBoneRole role = HumanoidBoneRole::None;
        std::string boneName;                       // bone name in the owning skeleton

        // Local-space rotation of this bone in the skeleton's reference (T/A) pose.
        // Captured at map time so the retarget delta is rest-relative; this is what
        // lets an A-pose source drive a T-pose target (and vice-versa) correctly.
        glm::quat referenceLocalRotation{1.0f, 0.0f, 0.0f, 0.0f};

        // When true, this bone's translation is retargeted (scaled by limb-length
        // ratio). Defaults true only for Hips; all other bones keep target bone
        // lengths (rotation-only), which is the standard humanoid behavior.
        bool retargetTranslation = false;
    };

    struct HumanoidRigData
    {
        std::string name;
        std::string sourceSkeletonAssetGuid;        // hex GUID of the .vfMesh this profile describes
        std::vector<HumanoidBoneBinding> bindings;
        float referenceHeight = 1.0f;               // model-height proxy for hip-height normalization

        const HumanoidBoneBinding* find(HumanoidBoneRole role) const noexcept
        {
            for (const auto& b : bindings)
                if (b.role == role)
                    return &b;
            return nullptr;
        }

        bool hasRole(HumanoidBoneRole role) const noexcept { return find(role) != nullptr; }
    };

    // Per-role tweak on a specific source->target pairing.
    struct RetargetRoleOverride
    {
        HumanoidBoneRole role = HumanoidBoneRole::None;
        bool enabled = true;                        // false skips this role entirely
        bool overrideTranslation = false;           // force-enable translation retarget for this role
    };

    struct RetargetMapData
    {
        std::string name;
        std::string sourceRigAssetGuid;             // .vfrig the clip was authored for
        std::string targetRigAssetGuid;             // .vfrig of the skeleton to drive
        std::vector<RetargetRoleOverride> overrides;
        bool bakeRootMotion = false;

        const RetargetRoleOverride* findOverride(HumanoidBoneRole role) const noexcept
        {
            for (const auto& o : overrides)
                if (o.role == role)
                    return &o;
            return nullptr;
        }
    };
}

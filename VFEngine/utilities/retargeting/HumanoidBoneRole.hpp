#pragma once
#include <cstdint>
#include <string>

// Standard humanoid rig definition (VK-910 animation retargeting).
//
// A HumanoidBoneRole is a skeleton-agnostic semantic bone slot (Hips, Head,
// LeftUpperArm, ...). Each concrete skeleton maps its own bone names to these
// roles in a HumanoidRigData (.vfrig). Retargeting flows source -> role -> target,
// so any humanoid clip can drive any humanoid skeleton through the shared role
// space. Roles and the required set mirror Unity Mecanim's Humanoid avatar.
namespace retargeting
{
    enum class HumanoidBoneRole : uint8_t
    {
        None = 0,

        // --- Core spine (Hips, Spine, Head required; Chest/UpperChest/Neck optional) ---
        Hips,
        Spine,
        Chest,
        UpperChest,
        Neck,
        Head,

        // --- Left arm (UpperArm/LowerArm/Hand required; Shoulder optional) ---
        LeftShoulder,
        LeftUpperArm,
        LeftLowerArm,
        LeftHand,

        // --- Right arm ---
        RightShoulder,
        RightUpperArm,
        RightLowerArm,
        RightHand,

        // --- Left leg (UpperLeg/LowerLeg/Foot required; Toes optional) ---
        LeftUpperLeg,
        LeftLowerLeg,
        LeftFoot,
        LeftToes,

        // --- Right leg ---
        RightUpperLeg,
        RightLowerLeg,
        RightFoot,
        RightToes,

        // --- Head detail (all optional) ---
        LeftEye,
        RightEye,
        Jaw,

        Count
    };

    inline constexpr uint8_t humanoidBoneRoleCount = static_cast<uint8_t>(HumanoidBoneRole::Count);

    inline constexpr const char* humanoidBoneRoleName(HumanoidBoneRole role) noexcept
    {
        switch (role)
        {
        case HumanoidBoneRole::None:          return "None";
        case HumanoidBoneRole::Hips:          return "Hips";
        case HumanoidBoneRole::Spine:         return "Spine";
        case HumanoidBoneRole::Chest:         return "Chest";
        case HumanoidBoneRole::UpperChest:    return "UpperChest";
        case HumanoidBoneRole::Neck:          return "Neck";
        case HumanoidBoneRole::Head:          return "Head";
        case HumanoidBoneRole::LeftShoulder:  return "LeftShoulder";
        case HumanoidBoneRole::LeftUpperArm:  return "LeftUpperArm";
        case HumanoidBoneRole::LeftLowerArm:  return "LeftLowerArm";
        case HumanoidBoneRole::LeftHand:      return "LeftHand";
        case HumanoidBoneRole::RightShoulder: return "RightShoulder";
        case HumanoidBoneRole::RightUpperArm: return "RightUpperArm";
        case HumanoidBoneRole::RightLowerArm: return "RightLowerArm";
        case HumanoidBoneRole::RightHand:     return "RightHand";
        case HumanoidBoneRole::LeftUpperLeg:  return "LeftUpperLeg";
        case HumanoidBoneRole::LeftLowerLeg:  return "LeftLowerLeg";
        case HumanoidBoneRole::LeftFoot:      return "LeftFoot";
        case HumanoidBoneRole::LeftToes:      return "LeftToes";
        case HumanoidBoneRole::RightUpperLeg: return "RightUpperLeg";
        case HumanoidBoneRole::RightLowerLeg: return "RightLowerLeg";
        case HumanoidBoneRole::RightFoot:     return "RightFoot";
        case HumanoidBoneRole::RightToes:     return "RightToes";
        case HumanoidBoneRole::LeftEye:       return "LeftEye";
        case HumanoidBoneRole::RightEye:      return "RightEye";
        case HumanoidBoneRole::Jaw:           return "Jaw";
        default:                              return "None";
        }
    }

    inline HumanoidBoneRole humanoidBoneRoleFromName(const std::string& name) noexcept
    {
        for (uint8_t i = 0; i < humanoidBoneRoleCount; ++i)
        {
            const auto role = static_cast<HumanoidBoneRole>(i);
            if (name == humanoidBoneRoleName(role))
                return role;
        }
        return HumanoidBoneRole::None;
    }

    // Mecanim-style "required" set: a profile missing any of these cannot retarget
    // a full-body humanoid clip. Optional roles improve fidelity but may be absent.
    inline constexpr bool isRequiredRole(HumanoidBoneRole role) noexcept
    {
        switch (role)
        {
        case HumanoidBoneRole::Hips:
        case HumanoidBoneRole::Spine:
        case HumanoidBoneRole::Head:
        case HumanoidBoneRole::LeftUpperArm:
        case HumanoidBoneRole::LeftLowerArm:
        case HumanoidBoneRole::LeftHand:
        case HumanoidBoneRole::RightUpperArm:
        case HumanoidBoneRole::RightLowerArm:
        case HumanoidBoneRole::RightHand:
        case HumanoidBoneRole::LeftUpperLeg:
        case HumanoidBoneRole::LeftLowerLeg:
        case HumanoidBoneRole::LeftFoot:
        case HumanoidBoneRole::RightUpperLeg:
        case HumanoidBoneRole::RightLowerLeg:
        case HumanoidBoneRole::RightFoot:
            return true;
        default:
            return false;
        }
    }
}

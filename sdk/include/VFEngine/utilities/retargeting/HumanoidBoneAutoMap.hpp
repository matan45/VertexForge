#pragma once
#include <vector>
#include "RetargetTypes.hpp"

namespace resource { struct SkeletonData; }

namespace retargeting
{
    // Best-effort assignment of a skeleton's bone names to humanoid roles, using a
    // normalize + synonym-match + hierarchy-tiebreak heuristic that handles the
    // common DCC conventions (Mixamo "mixamorig:LeftForeArm", UE "lowerarm_l",
    // Blender "Arm.L", 3ds Max "Bip01 L Forearm"). Returns one binding per resolved
    // role (unmatched roles are omitted so the editor can flag missing required roles).
    // referenceLocalRotation is captured from the skeleton's local bind pose, and
    // Hips defaults to retargetTranslation = true.
    std::vector<HumanoidBoneBinding> autoMapHumanoidBones(const resource::SkeletonData& skeleton);
}

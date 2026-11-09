#pragma once
#include "RetargetTypes.hpp"
#include <optional>
#include <string>
#include <unordered_map>

// Which source-rig bone drives a target-skeleton bone through a retarget map (VK-910).
// Single source of truth for animation::RetargetContext::build (runtime) and the MCP
// clip-compatibility check (VK-1656). Header-only.
namespace retargeting
{
    struct RetargetBoneMatch
    {
        const HumanoidBoneBinding* target = nullptr; // target-rig binding of the target bone
        const HumanoidBoneBinding* source = nullptr; // source-rig binding of the same role
    };

    class RetargetBoneMapping
    {
    public:
        // Keeps references: sourceRig, targetRig and map must outlive this object.
        RetargetBoneMapping(const HumanoidRigData& sourceRig, const HumanoidRigData& targetRig,
                            const RetargetMapData& map)
            : sourceRig(sourceRig), map(map)
        {
            targetByName.reserve(targetRig.bindings.size());
            for (const auto& binding : targetRig.bindings)
                targetByName[binding.boneName] = &binding; // a repeated bone name: the last binding wins
        }

        // The bindings that drive `targetBoneName`, or nullopt when that bone holds its bind
        // pose: the target rig does not bind it, a map override disables its role, or the
        // source rig has no bone for the role.
        std::optional<RetargetBoneMatch> match(const std::string& targetBoneName) const
        {
            const auto it = targetByName.find(targetBoneName);
            if (it == targetByName.end())
                return std::nullopt;

            const HumanoidBoneRole role = it->second->role;
            if (const auto* roleOverride = map.findOverride(role); roleOverride && !roleOverride->enabled)
                return std::nullopt;

            const HumanoidBoneBinding* source = sourceRig.find(role);
            if (source == nullptr)
                return std::nullopt;

            return RetargetBoneMatch{it->second, source};
        }

    private:
        const HumanoidRigData& sourceRig;
        const RetargetMapData& map;
        std::unordered_map<std::string, const HumanoidBoneBinding*> targetByName;
    };
}

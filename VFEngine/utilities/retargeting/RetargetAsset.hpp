#pragma once
#include "RetargetTypes.hpp"
#include <string_view>
#include <optional>

namespace resource { struct SkeletonData; }

namespace retargeting
{
    inline constexpr const char* RIG_FORMAT_VERSION = "1.0";
    inline constexpr const char* RETARGET_FORMAT_VERSION = "1.0";

    // .vfrig — per-skeleton Humanoid Rig Profile (JSON).
    class HumanoidRigAsset
    {
    public:
        static std::optional<HumanoidRigData> load(std::string_view path);
        static bool save(std::string_view path, const HumanoidRigData& rig);

        // Build a default profile from a skeleton via the name auto-map heuristic.
        static HumanoidRigData createFromSkeleton(const resource::SkeletonData& skeleton,
                                                  const std::string& sourceSkeletonGuidHex);
    };

    // .vfretarget — source-rig -> target-rig binding (JSON).
    class RetargetMapAsset
    {
    public:
        static std::optional<RetargetMapData> load(std::string_view path);
        static bool save(std::string_view path, const RetargetMapData& map);
    };
}

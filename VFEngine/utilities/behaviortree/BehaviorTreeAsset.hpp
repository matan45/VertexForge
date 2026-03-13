#pragma once

#include "BehaviorTreeTypes.hpp"
#include <string_view>
#include <string>
#include <optional>

namespace behaviortree
{
    inline constexpr const char* BT_FORMAT_VERSION = "1.0";

    class BehaviorTreeAsset
    {
    public:
        static std::optional<BehaviorTreeData> load(std::string_view path);
        static bool save(std::string_view path, const BehaviorTreeData& data);
        static BehaviorTreeData createDefault(const std::string& name = "New Behavior Tree");
    };
}

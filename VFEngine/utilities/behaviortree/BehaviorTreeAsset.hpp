#pragma once

#include "BehaviorTreeTypes.hpp"
#include <nlohmann/json_fwd.hpp>
#include <string_view>
#include <optional>
#include <functional>

namespace behaviortree
{
    inline constexpr const char* BT_FORMAT_VERSION = "1.0";

    class BehaviorTreeAsset
    {
    public:
        static std::optional<BehaviorTreeData> load(std::string_view path);
        static bool save(std::string_view path, const BehaviorTreeData& data);
        static BehaviorTreeData createDefault(const std::string& name = "New Behavior Tree");

    private:
        using WarningLogger = std::function<void(const std::string&)>;

        // Serialization helpers
        static nlohmann::json serializeNode(const BTNode& node);
        static BTNode deserializeNode(const nlohmann::json& j);

        static nlohmann::json serializeLink(const BTLink& link);
        static BTLink deserializeLink(const nlohmann::json& j);

        static nlohmann::json serializeBlackboardKey(const BlackboardKeyDef& keyDef);
        static BlackboardKeyDef deserializeBlackboardKey(const nlohmann::json& j);

        static nlohmann::json serializeBlackboardValue(const BlackboardValue& val, BlackboardValueType type);
        static BlackboardValue deserializeBlackboardValue(const nlohmann::json& j, BlackboardValueType type);

        static nlohmann::json serializeProperties(const std::unordered_map<std::string, BlackboardValue>& properties);
        static std::unordered_map<std::string, BlackboardValue> deserializeProperties(const nlohmann::json& j);

        // Load helpers
        static std::optional<nlohmann::json> readJsonFromFile(std::string_view path);
        static BehaviorTreeData parseBehaviorTreeData(const nlohmann::json& j, const WarningLogger& logWarning);
        static void parseNodes(const nlohmann::json& j, BTGraph& graph, const WarningLogger& logWarning);
        static void parseLinks(const nlohmann::json& j, BTGraph& graph, const WarningLogger& logWarning);
        static void parseBlackboardKeys(const nlohmann::json& j, BTGraph& graph, const WarningLogger& logWarning);

        // Save helpers
        static nlohmann::json buildBehaviorTreeJson(const BehaviorTreeData& data);
    };
}

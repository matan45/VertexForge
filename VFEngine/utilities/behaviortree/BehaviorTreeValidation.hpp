#pragma once

#include "BehaviorTreeTypes.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace behaviortree::validation
{
    enum class Severity
    {
        Error,
        Warning,
        Info
    };

    struct Diagnostic
    {
        Severity severity = Severity::Info;
        uint32_t nodeId = 0; // 0 = graph-level diagnostic
        std::string message;
    };

    struct ValidationReport
    {
        std::vector<Diagnostic> diagnostics;

        [[nodiscard]] bool hasErrors() const
        {
            return std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d)
            {
                return d.severity == Severity::Error;
            });
        }

        [[nodiscard]] bool hasWarnings() const
        {
            return std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d)
            {
                return d.severity == Severity::Warning;
            });
        }

        [[nodiscard]] bool ok() const { return !hasErrors(); }

        [[nodiscard]] int errorCount() const
        {
            return static_cast<int>(std::count_if(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d)
            {
                return d.severity == Severity::Error;
            }));
        }

        [[nodiscard]] int warningCount() const
        {
            return static_cast<int>(std::count_if(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d)
            {
                return d.severity == Severity::Warning;
            }));
        }

        [[nodiscard]] std::unordered_map<uint32_t, Severity> worstByNode() const
        {
            std::unordered_map<uint32_t, Severity> result;
            for (const auto& diagnostic : diagnostics)
            {
                if (diagnostic.nodeId == 0) continue;

                auto it = result.find(diagnostic.nodeId);
                if (it == result.end() || severityRank(diagnostic.severity) < severityRank(it->second))
                {
                    result[diagnostic.nodeId] = diagnostic.severity;
                }
            }
            return result;
        }

    private:
        static int severityRank(Severity severity)
        {
            switch (severity)
            {
            case Severity::Error: return 0;
            case Severity::Warning: return 1;
            case Severity::Info: return 2;
            default: return 2;
            }
        }
    };

    struct ValidationContext
    {
        std::function<bool(std::string_view treePath)> subtreeExists;
        bool allowSubTrees = true;
    };

    namespace detail
    {
        inline void add(ValidationReport& report, Severity severity, uint32_t nodeId, std::string message)
        {
            report.diagnostics.push_back(Diagnostic{severity, nodeId, std::move(message)});
        }

        inline bool hasStringProperty(const BTNode& node, const std::string& key, std::string& value)
        {
            auto it = node.properties.find(key);
            if (it == node.properties.end() || !std::holds_alternative<std::string>(it->second))
            {
                value.clear();
                return false;
            }
            value = std::get<std::string>(it->second);
            return true;
        }

        inline const BlackboardKeyDef* findKey(const BTGraph& graph, const std::string& name)
        {
            for (const auto& key : graph.blackboardKeys)
            {
                if (key.name == name) return &key;
            }
            return nullptr;
        }

        inline BlackboardValueType valueType(const BlackboardValue& value)
        {
            static_assert(static_cast<size_t>(BlackboardValueType::Float) == 0);
            static_assert(static_cast<size_t>(BlackboardValueType::Int) == 1);
            static_assert(static_cast<size_t>(BlackboardValueType::Bool) == 2);
            static_assert(static_cast<size_t>(BlackboardValueType::String) == 3);
            static_assert(static_cast<size_t>(BlackboardValueType::Vec3) == 4);
            static_assert(static_cast<size_t>(BlackboardValueType::Entity) == 5);
            return static_cast<BlackboardValueType>(value.index());
        }

        inline bool valueMatchesType(const BlackboardValue& value, BlackboardValueType type)
        {
            return valueType(value) == type;
        }

        inline bool isValidCompareOp(const std::string& op)
        {
            return op == "==" || op == "!=" || op == ">" || op == "<" || op == ">=" || op == "<=";
        }

        inline bool isNumericCompareOp(const std::string& op)
        {
            return op == ">" || op == "<" || op == ">=" || op == "<=";
        }

        inline bool isValidAbortMode(const std::string& mode)
        {
            return mode == "None" || mode == "Self" || mode == "LowerPriority" || mode == "Both";
        }

        inline bool isValidParallelPolicy(const std::string& policy)
        {
            return policy == "RequireAll" || policy == "RequireOne";
        }

        inline bool isValidLogLevel(const std::string& level)
        {
            return level == "Info" || level == "Warn" || level == "Error";
        }

        inline bool isValidServiceType(const std::string& type)
        {
            return type == "EQSRefresh" || type == "LineOfSightRefresh" || type == "FocusUpdate";
        }

        inline void validateStringEnumProperty(ValidationReport& report,
                                               const BTNode& node,
                                               const std::string& key,
                                               const char* label,
                                               bool (*isValid)(const std::string&))
        {
            std::string value;
            if (hasStringProperty(node, key, value) && !isValid(value))
            {
                add(report, Severity::Warning, node.id,
                    std::string(label) + " property '" + key + "' has unsupported value '" + value + "'.");
            }
        }

        inline void validateReferencedKey(ValidationReport& report,
                                          const BTGraph& graph,
                                          const BTNode& node,
                                          const std::string& propertyName)
        {
            std::string key;
            if (!hasStringProperty(node, propertyName, key) || key.empty())
            {
                add(report, Severity::Warning, node.id,
                    "Missing or empty blackboard key property '" + propertyName + "'.");
                return;
            }

            if (!findKey(graph, key))
            {
                add(report, Severity::Warning, node.id,
                    "Blackboard key '" + key + "' referenced by '" + propertyName + "' is not declared.");
            }
        }

        inline void validateTypedValue(ValidationReport& report,
                                       const BTGraph& graph,
                                       const BTNode& node,
                                       const std::string& keyProperty,
                                       const std::string& valueProperty,
                                       bool compareValue)
        {
            std::string key;
            if (!hasStringProperty(node, keyProperty, key) || key.empty())
            {
                add(report, Severity::Warning, node.id,
                    "Cannot validate '" + valueProperty + "' because key property '" + keyProperty + "' is missing.");
                return;
            }

            const auto* keyDef = findKey(graph, key);
            if (!keyDef) return;

            auto valueIt = node.properties.find(valueProperty);
            if (valueIt == node.properties.end())
            {
                add(report, Severity::Warning, node.id,
                    "Missing blackboard value property '" + valueProperty + "'.");
                return;
            }

            if (!valueMatchesType(valueIt->second, keyDef->type))
            {
                add(report, Severity::Warning, node.id,
                    "Property '" + valueProperty + "' does not match declared type for blackboard key '" + key + "'.");
            }

            if (!compareValue) return;

            std::string op = "==";
            hasStringProperty(node, "compareOp", op);
            if (!isValidCompareOp(op))
            {
                add(report, Severity::Warning, node.id,
                    "Compare operator '" + op + "' is unsupported.");
                return;
            }

            if (keyDef->type == BlackboardValueType::Bool || keyDef->type == BlackboardValueType::String)
            {
                if (isNumericCompareOp(op))
                {
                    add(report, Severity::Warning, node.id,
                        "Compare operator '" + op + "' is numeric but key '" + key + "' is not numeric.");
                }
            }
            else if (keyDef->type == BlackboardValueType::Vec3 || keyDef->type == BlackboardValueType::Entity)
            {
                add(report, Severity::Warning, node.id,
                    "Runtime comparisons are not supported for Vec3 or Entity key '" + key + "'.");
            }
        }

        inline void dfsCycle(uint32_t nodeId,
                             const std::unordered_map<uint32_t, std::vector<uint32_t>>& adjacency,
                             std::unordered_map<uint32_t, uint8_t>& colors,
                             ValidationReport& report,
                             bool& foundCycle)
        {
            colors[nodeId] = 1;
            auto adjIt = adjacency.find(nodeId);
            if (adjIt != adjacency.end())
            {
                for (uint32_t childId : adjIt->second)
                {
                    uint8_t color = colors[childId];
                    if (color == 1)
                    {
                        foundCycle = true;
                        add(report, Severity::Error, childId, "Behavior tree graph contains a cycle.");
                    }
                    else if (color == 0)
                    {
                        dfsCycle(childId, adjacency, colors, report, foundCycle);
                    }
                }
            }
            colors[nodeId] = 2;
        }

        inline void markReachable(uint32_t nodeId,
                                  const std::unordered_map<uint32_t, std::vector<uint32_t>>& adjacency,
                                  std::unordered_set<uint32_t>& reachable)
        {
            if (!reachable.insert(nodeId).second) return;

            auto it = adjacency.find(nodeId);
            if (it == adjacency.end()) return;

            for (uint32_t childId : it->second)
            {
                markReachable(childId, adjacency, reachable);
            }
        }
    }

    inline ValidationReport validateBehaviorTree(const BehaviorTreeData& data,
                                                 const ValidationContext& context = {})
    {
        ValidationReport report;
        const BTGraph& graph = data.graph;

        std::unordered_map<uint32_t, int> nodeIdCounts;
        std::unordered_set<uint32_t> nodeIds;
        int rootTypeCount = 0;

        for (const auto& node : graph.nodes)
        {
            if (node.id == 0)
            {
                detail::add(report, Severity::Error, node.id, "Node id 0 is invalid.");
            }

            int count = ++nodeIdCounts[node.id];
            if (count == 2)
            {
                detail::add(report, Severity::Error, node.id,
                    "Duplicate node id " + std::to_string(node.id) + ".");
            }
            nodeIds.insert(node.id);

            if (isRootNode(node.type)) ++rootTypeCount;
        }

        if (graph.rootNodeId == 0)
        {
            detail::add(report, Severity::Error, 0, "Graph has no rootNodeId.");
        }
        else
        {
            const BTNode* root = graph.findNodeById(graph.rootNodeId);
            if (!root)
            {
                detail::add(report, Severity::Error, 0,
                    "Root node id " + std::to_string(graph.rootNodeId) + " does not exist.");
            }
            else if (!isRootNode(root->type))
            {
                detail::add(report, Severity::Warning, root->id,
                    "Graph rootNodeId points to a non-Root node.");
            }
        }

        if (rootTypeCount == 0)
        {
            detail::add(report, Severity::Warning, 0, "Graph has no Root node.");
        }
        else if (rootTypeCount > 1)
        {
            detail::add(report, Severity::Warning, 0,
                "Graph contains multiple Root nodes; only rootNodeId is used.");
        }

        std::unordered_map<uint32_t, int> linkIdCounts;
        std::unordered_map<uint32_t, int> incomingCounts;
        std::unordered_map<uint32_t, std::vector<uint32_t>> adjacency;

        for (const auto& link : graph.links)
        {
            if (++linkIdCounts[link.id] == 2)
            {
                detail::add(report, Severity::Warning, 0,
                    "Duplicate link id " + std::to_string(link.id) + ".");
            }

            bool validSource = nodeIds.find(link.sourceNodeId) != nodeIds.end();
            bool validTarget = nodeIds.find(link.targetNodeId) != nodeIds.end();

            if (!validSource)
            {
                detail::add(report, Severity::Error, 0,
                    "Link " + std::to_string(link.id) + " has missing source node " +
                    std::to_string(link.sourceNodeId) + ".");
            }
            if (!validTarget)
            {
                detail::add(report, Severity::Error, 0,
                    "Link " + std::to_string(link.id) + " has missing target node " +
                    std::to_string(link.targetNodeId) + ".");
            }
            if (link.sourceNodeId == link.targetNodeId)
            {
                detail::add(report, Severity::Error, link.sourceNodeId,
                    "Node links to itself.");
            }

            if (validSource && validTarget)
            {
                adjacency[link.sourceNodeId].push_back(link.targetNodeId);
                ++incomingCounts[link.targetNodeId];
            }
        }

        for (const auto& [nodeId, count] : incomingCounts)
        {
            if (count > 1)
            {
                detail::add(report, Severity::Error, nodeId,
                    "Node has multiple incoming links.");
            }
        }

        std::unordered_map<uint32_t, uint8_t> colors;
        for (uint32_t nodeId : nodeIds)
        {
            colors[nodeId] = 0;
        }
        bool foundCycle = false;
        for (uint32_t nodeId : nodeIds)
        {
            if (colors[nodeId] == 0)
            {
                detail::dfsCycle(nodeId, adjacency, colors, report, foundCycle);
            }
        }
        (void)foundCycle;

        std::unordered_set<uint32_t> reachable;
        if (graph.rootNodeId != 0 && nodeIds.find(graph.rootNodeId) != nodeIds.end())
        {
            detail::markReachable(graph.rootNodeId, adjacency, reachable);
        }

        for (const auto& node : graph.nodes)
        {
            auto childIt = adjacency.find(node.id);
            const size_t childCount = childIt == adjacency.end() ? 0u : childIt->second.size();

            if (isRootNode(node.type))
            {
                if (childCount == 0)
                {
                    detail::add(report, Severity::Error, node.id, "Root node has no child.");
                }
                else if (childCount > 1)
                {
                    detail::add(report, Severity::Warning, node.id,
                        "Root node has more than one child; runtime uses the first child.");
                }
            }
            else if (isCompositeNode(node.type))
            {
                if (childCount == 0)
                {
                    detail::add(report, Severity::Error, node.id,
                        "Composite node has no children.");
                }
            }
            else if (isDecoratorNode(node.type))
            {
                if (childCount == 0)
                {
                    detail::add(report, Severity::Error, node.id,
                        "Decorator node has no child.");
                }
                else if (childCount > 1)
                {
                    detail::add(report, Severity::Warning, node.id,
                        "Decorator node has more than one child; runtime uses the first child.");
                }
            }
            else if (isServiceNode(node.type))
            {
                if (childCount == 0)
                {
                    detail::add(report, Severity::Error, node.id,
                        "Service node has no child.");
                }
                else if (childCount > 1)
                {
                    detail::add(report, Severity::Warning, node.id,
                        "Service node has more than one child; runtime uses the first child.");
                }
            }
            else if (isTaskNode(node.type) && childCount > 0)
            {
                detail::add(report, Severity::Warning, node.id,
                    "Task node has children but runtime treats tasks as leaves.");
            }

            if (graph.rootNodeId != 0 && reachable.find(node.id) == reachable.end())
            {
                detail::add(report, Severity::Info, node.id, "Node is unreachable from the root.");
            }
        }

        std::unordered_set<std::string> keyNames;
        for (const auto& keyDef : graph.blackboardKeys)
        {
            if (keyDef.name.empty())
            {
                detail::add(report, Severity::Warning, 0, "Blackboard key has an empty name.");
            }
            if (!keyNames.insert(keyDef.name).second)
            {
                detail::add(report, Severity::Warning, 0,
                    "Duplicate blackboard key '" + keyDef.name + "'.");
            }
            if (!detail::valueMatchesType(keyDef.defaultValue, keyDef.type))
            {
                detail::add(report, Severity::Warning, 0,
                    "Default value for blackboard key '" + keyDef.name + "' does not match its declared type.");
            }
        }

        for (const auto& node : graph.nodes)
        {
            switch (node.type)
            {
            case BTNodeType::SetBlackboardValue:
                detail::validateReferencedKey(report, graph, node, "key");
                detail::validateTypedValue(report, graph, node, "key", "value", false);
                break;
            case BTNodeType::CheckBlackboardValue:
                detail::validateReferencedKey(report, graph, node, "key");
                detail::validateTypedValue(report, graph, node, "key", "compareValue", true);
                break;
            case BTNodeType::BlackboardCondition:
                detail::validateReferencedKey(report, graph, node, "key");
                detail::validateTypedValue(report, graph, node, "key", "compareValue", true);
                detail::validateStringEnumProperty(report, node, "abortMode", "BlackboardCondition", detail::isValidAbortMode);
                break;
            case BTNodeType::MoveTo:
                detail::validateReferencedKey(report, graph, node, "targetKey");
                break;
            case BTNodeType::LineOfSight:
                detail::validateReferencedKey(report, graph, node, "targetKey");
                break;
            case BTNodeType::EnvironmentQuery:
                detail::validateReferencedKey(report, graph, node, "resultKey");
                break;
            case BTNodeType::SubTree:
            {
                std::string treePath;
                if (!context.allowSubTrees)
                {
                    detail::add(report, Severity::Error, node.id,
                        "Expanded runtime tree still contains a SubTree node.");
                }
                if (!detail::hasStringProperty(node, "treePath", treePath) || treePath.empty())
                {
                    detail::add(report, Severity::Error, node.id,
                        "SubTree node has no treePath.");
                }
                else if (context.subtreeExists && !context.subtreeExists(treePath))
                {
                    detail::add(report, Severity::Error, node.id,
                        "SubTree file '" + treePath + "' does not exist.");
                }
                break;
            }
            case BTNodeType::Parallel:
                detail::validateStringEnumProperty(report, node, "policy", "Parallel", detail::isValidParallelPolicy);
                break;
            case BTNodeType::Log:
                detail::validateStringEnumProperty(report, node, "level", "Log", detail::isValidLogLevel);
                break;
            case BTNodeType::ScriptTask:
                if (node.scriptPath.empty())
                {
                    detail::add(report, Severity::Warning, node.id, "ScriptTask has an empty scriptPath.");
                }
                if (node.scriptClassName.empty())
                {
                    detail::add(report, Severity::Warning, node.id, "ScriptTask has an empty scriptClassName.");
                }
                break;
            case BTNodeType::Service:
            {
                float interval = 0.0f;
                bool hasInterval = false;
                {
                    auto it = node.properties.find("interval");
                    if (it != node.properties.end() && std::holds_alternative<float>(it->second))
                    {
                        interval = std::get<float>(it->second);
                        hasInterval = true;
                    }
                }
                if (!hasInterval)
                {
                    detail::add(report, Severity::Error, node.id, "Service node is missing a float 'interval' property.");
                }
                else if (interval <= 0.0f)
                {
                    detail::add(report, Severity::Error, node.id, "Service 'interval' must be greater than zero.");
                }

                float deviation = 0.0f;
                {
                    auto it = node.properties.find("randomDeviation");
                    if (it != node.properties.end() && std::holds_alternative<float>(it->second))
                        deviation = std::get<float>(it->second);
                }
                if (deviation < 0.0f)
                {
                    detail::add(report, Severity::Warning, node.id,
                        "Service 'randomDeviation' is negative; runtime treats it as no deviation.");
                }
                else if (hasInterval && interval > 0.0f && deviation >= interval)
                {
                    detail::add(report, Severity::Warning, node.id,
                        "Service 'randomDeviation' >= 'interval' can yield near-zero fire intervals.");
                }

                detail::validateStringEnumProperty(report, node, "serviceType", "Service", detail::isValidServiceType);

                std::string serviceType;
                detail::hasStringProperty(node, "serviceType", serviceType);
                if (serviceType == "EQSRefresh")
                {
                    detail::validateReferencedKey(report, graph, node, "resultKey");
                }
                else if (serviceType == "LineOfSightRefresh")
                {
                    detail::validateReferencedKey(report, graph, node, "targetKey");
                    detail::validateReferencedKey(report, graph, node, "visibilityKey");
                }
                else if (serviceType == "FocusUpdate")
                {
                    detail::validateReferencedKey(report, graph, node, "targetKey");
                    detail::validateReferencedKey(report, graph, node, "focusKey");
                }
                break;
            }
            default:
                break;
            }
        }

        return report;
    }
}

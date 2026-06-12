#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <cstdint>
#include <glm/glm.hpp>
#include "../../services/data/EntityHandle.hpp"

namespace behaviortree
{
    enum class BTNodeType : uint8_t
    {
        Root,

        Sequence,
        Selector,
        Parallel,

        Inverter,
        Repeater,
        Succeeder,
        RepeatUntilFail,
        Cooldown,
        TimeLimit,
        BlackboardCondition,

        Wait,
        Log,
        MoveTo,
        PlayAnimation,
        SetBlackboardValue,
        CheckBlackboardValue,
        ScriptTask,
        EnvironmentQuery,
        LineOfSight,
        SubTree
    };

    enum class BTNodeStatus : uint8_t
    {
        Success,
        Failure,
        Running
    };

    enum class ParallelPolicy : uint8_t
    {
        RequireAll,
        RequireOne
    };

    enum class BlackboardValueType : uint8_t
    {
        Float,
        Int,
        Bool,
        String,
        Vec3,
        Entity
    };

    enum class CompareOp : uint8_t
    {
        Equal,
        NotEqual,
        Greater,
        Less,
        GreaterEqual,
        LessEqual
    };

    // Observer-abort behavior for BlackboardCondition decorators:
    // - Self: while the guarded subtree runs, re-evaluate each tick and abort it when the condition turns false
    // - LowerPriority: when this condition (as a Selector child) becomes true, abort the running lower-priority sibling branch
    enum class AbortMode : uint8_t
    {
        None,
        Self,
        LowerPriority,
        Both
    };

    enum class LogLevel : uint8_t
    {
        Info,
        Warn,
        Error
    };

    using BlackboardValue = std::variant<float, int32_t, bool, std::string, glm::vec3, services::EntityHandle>;

    struct BTNode
    {
        uint32_t id = 0;
        BTNodeType type = BTNodeType::Sequence;
        std::string name;
        glm::vec2 position{0.0f, 0.0f};

        std::unordered_map<std::string, BlackboardValue> properties;

        std::string scriptPath;
        std::string scriptClassName;
    };

    struct BTLink
    {
        uint32_t id = 0;
        uint32_t sourceNodeId = 0;
        uint32_t targetNodeId = 0;
        uint32_t sortOrder = 0;
    };

    struct BlackboardKeyDef
    {
        std::string name;
        BlackboardValueType type = BlackboardValueType::Float;
        BlackboardValue defaultValue = 0.0f;
    };

    struct BTGraph
    {
        std::vector<BTNode> nodes;
        std::vector<BTLink> links;
        uint32_t rootNodeId = 0;
        uint32_t nextNodeId = 1;
        uint32_t nextLinkId = 1;

        std::vector<BlackboardKeyDef> blackboardKeys;

        BTNode* findNodeById(uint32_t id);
        const BTNode* findNodeById(uint32_t id) const;

        std::vector<const BTNode*> getChildren(uint32_t nodeId) const;
        std::vector<BTNode*> getChildren(uint32_t nodeId);
        const BTNode* getParent(uint32_t nodeId) const;
    };

    struct BehaviorTreeData
    {
        std::string version = "1.0";
        std::string name;
        BTGraph graph;
    };

    // Point-in-time view of a live runtime for the editor debugger.
    // Copied under a lock because trees tick on a worker task while ImGui reads on the main thread.
    struct BTRuntimeSnapshot
    {
        bool valid = false;
        uint64_t tickIndex = 0;
        std::unordered_map<uint32_t, BTNodeStatus> nodeStatuses;
        std::vector<std::pair<std::string, BlackboardValue>> blackboard;
    };

    bool isCompositeNode(BTNodeType type);
    bool isDecoratorNode(BTNodeType type);
    bool isTaskNode(BTNodeType type);
    bool isRootNode(BTNodeType type);
    bool hasOutputPin(BTNodeType type);

    const char* nodeTypeToString(BTNodeType type);
    BTNodeType stringToNodeType(const std::string& str);

    const char* blackboardValueTypeToString(BlackboardValueType type);
    BlackboardValueType stringToBlackboardValueType(const std::string& str);

    const char* compareOpToString(CompareOp op);
    CompareOp stringToCompareOp(const std::string& str);

    const char* abortModeToString(AbortMode mode);
    AbortMode stringToAbortMode(const std::string& str);

    const char* logLevelToString(LogLevel level);
    LogLevel stringToLogLevel(const std::string& str);

    const char* parallelPolicyToString(ParallelPolicy policy);
    ParallelPolicy stringToParallelPolicy(const std::string& str);
}

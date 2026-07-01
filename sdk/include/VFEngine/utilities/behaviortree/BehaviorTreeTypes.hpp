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
        SubTree,

        // Branch-scoped service (VK-1456): a single-child passthrough that runs periodic service
        // logic (EQS/perception refresh) while its branch is on the active tick path.
        Service,

        // Runtime-swappable subtree (VK-1457). Unlike SubTree (inlined at load), this is a task leaf
        // that resolves its target tree at runtime (blackboard key / injection tag / default path) and
        // runs it as a nested runtime with an isolated blackboard. Appended LAST so existing enum
        // values never renumber.
        DynamicSubTree
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

    // Direction of an explicit parent<->child blackboard parameter mapping on a DynamicSubTree node
    // (VK-1457). In: copy parent->child before the nested tick. Out: copy child->parent after. InOut: both.
    enum class MappingDirection : uint8_t
    {
        In,
        Out,
        InOut
    };

    // One explicit blackboard parameter binding between a DynamicSubTree's parent tree and its
    // isolated child blackboard. Replaces reliance on same-name key merging for dynamic subtrees.
    struct BlackboardMapping
    {
        std::string parentKey;
        std::string childKey;
        MappingDirection direction = MappingDirection::In;
    };

    struct BTNode
    {
        uint32_t id = 0;
        BTNodeType type = BTNodeType::Sequence;
        std::string name;
        glm::vec2 position{0.0f, 0.0f};

        std::unordered_map<std::string, BlackboardValue> properties;

        std::string scriptPath;
        std::string scriptClassName;

        // Explicit parent<->child blackboard parameter mappings for DynamicSubTree nodes (VK-1457).
        // Empty for every other node type (and for old assets — see BehaviorTreeAsset deserialization).
        std::vector<BlackboardMapping> blackboardMappings;
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

    // One recorded abort/interruption for the runtime debugger (VK-1457).
    struct BTAbortRecord
    {
        uint32_t nodeId = 0;
        uint64_t tickIndex = 0;
        std::string reason;
    };

    // Kind of execution-history event captured for the runtime debugger (VK-1457).
    enum class BTEventType : uint8_t
    {
        Enter,       // a task node started running (transitioned into Running)
        Exit,        // a task node finished (transitioned out of Running to a terminal status)
        Abort,       // a running node was aborted / preempted
        ServiceFire  // a Service node fired its periodic logic
    };

    // One entry in the bounded execution-history ring buffer for the runtime debugger (VK-1457).
    struct BTExecutionEvent
    {
        uint64_t tickIndex = 0;
        uint32_t nodeId = 0;
        BTEventType type = BTEventType::Enter;
        BTNodeStatus status = BTNodeStatus::Running;
    };

    // Point-in-time view of a live runtime for the editor debugger.
    // Copied under a lock because trees tick on a worker task while ImGui reads on the main thread.
    struct BTRuntimeSnapshot
    {
        bool valid = false;
        uint64_t tickIndex = 0;
        std::unordered_map<uint32_t, BTNodeStatus> nodeStatuses;
        std::vector<std::pair<std::string, BlackboardValue>> blackboard;

        // VK-1457 richer debug data.
        std::vector<uint32_t> activePath;                       // root -> deepest running leaf
        std::unordered_map<uint32_t, int> currentChildIndices;  // composite resume points
        std::unordered_map<uint32_t, float> elapsedTimes;       // per-node elapsed timer
        std::unordered_map<uint32_t, BTNodeStatus> lastResults; // last COMPLETED (non-Running) result
        std::vector<BTAbortRecord> abortRecords;                // recent aborts (bounded)
        std::vector<BTExecutionEvent> executionEvents;          // bounded history ring
        bool paused = false;                                    // debugger pause state for this target
        std::string activeDynamicSubtreePath;                   // path of the running dynamic subtree, if any
    };

    bool isCompositeNode(BTNodeType type);
    bool isDecoratorNode(BTNodeType type);
    bool isTaskNode(BTNodeType type);
    bool isRootNode(BTNodeType type);
    bool isServiceNode(BTNodeType type);
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

    const char* mappingDirectionToString(MappingDirection direction);
    MappingDirection stringToMappingDirection(const std::string& str);
}

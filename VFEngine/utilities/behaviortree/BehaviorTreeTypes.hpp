#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <optional>
#include <cstdint>
#include <glm/glm.hpp>
#include "../../services/data/EntityHandle.hpp"

namespace behaviortree
{
    enum class BTNodeType : uint8_t
    {
        // Root
        Root,

        // Composites
        Sequence,
        Selector,
        Parallel,

        // Decorators
        Inverter,
        Repeater,
        Succeeder,
        RepeatUntilFail,
        Cooldown,
        TimeLimit,

        // Tasks / Leaves
        Wait,
        Log,
        MoveTo,
        PlayAnimation,
        SetBlackboardValue,
        CheckBlackboardValue,

        // Script task (mType)
        ScriptTask
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

        // Generic properties stored as variant map
        std::unordered_map<std::string, BlackboardValue> properties;

        // For ScriptTask nodes
        std::string scriptPath;
        std::string scriptClassName;
    };

    struct BTLink
    {
        uint32_t id = 0;
        uint32_t sourceNodeId = 0; // parent
        uint32_t targetNodeId = 0; // child
        uint32_t sortOrder = 0;    // left-to-right child ordering
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

        // Get children of a node sorted by sortOrder
        std::vector<const BTNode*> getChildren(uint32_t nodeId) const;
        std::vector<BTNode*> getChildren(uint32_t nodeId);

        // Get the parent node of a given node (nullptr if root)
        const BTNode* getParent(uint32_t nodeId) const;
    };

    struct BehaviorTreeData
    {
        std::string version = "1.0";
        std::string name;
        BTGraph graph;
    };

    // Category helpers
    bool isCompositeNode(BTNodeType type);
    bool isDecoratorNode(BTNodeType type);
    bool isTaskNode(BTNodeType type);
    bool isRootNode(BTNodeType type);
    bool hasOutputPin(BTNodeType type); // Can have children

    const char* nodeTypeToString(BTNodeType type);
    BTNodeType stringToNodeType(const std::string& str);

    const char* blackboardValueTypeToString(BlackboardValueType type);
    BlackboardValueType stringToBlackboardValueType(const std::string& str);

    const char* compareOpToString(CompareOp op);
    CompareOp stringToCompareOp(const std::string& str);

    const char* logLevelToString(LogLevel level);
    LogLevel stringToLogLevel(const std::string& str);

    const char* parallelPolicyToString(ParallelPolicy policy);
    ParallelPolicy stringToParallelPolicy(const std::string& str);
}

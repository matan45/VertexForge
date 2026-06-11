#include "BehaviorTreeTypes.hpp"
#include <algorithm>

namespace behaviortree
{
    BTNode* BTGraph::findNodeById(uint32_t id)
    {
        for (auto& node : nodes)
        {
            if (node.id == id) return &node;
        }
        return nullptr;
    }

    const BTNode* BTGraph::findNodeById(uint32_t id) const
    {
        for (const auto& node : nodes)
        {
            if (node.id == id) return &node;
        }
        return nullptr;
    }

    std::vector<const BTNode*> BTGraph::getChildren(uint32_t nodeId) const
    {
        // Collect links from this node, sorted by sortOrder
        std::vector<const BTLink*> childLinks;
        for (const auto& link : links)
        {
            if (link.sourceNodeId == nodeId)
            {
                childLinks.push_back(&link);
            }
        }

        std::sort(childLinks.begin(), childLinks.end(),
                  [](const BTLink* a, const BTLink* b) { return a->sortOrder < b->sortOrder; });

        std::vector<const BTNode*> children;
        children.reserve(childLinks.size());
        for (const auto* link : childLinks)
        {
            const BTNode* child = findNodeById(link->targetNodeId);
            if (child)
            {
                children.push_back(child);
            }
        }
        return children;
    }

    std::vector<BTNode*> BTGraph::getChildren(uint32_t nodeId)
    {
        std::vector<const BTLink*> childLinks;
        for (const auto& link : links)
        {
            if (link.sourceNodeId == nodeId)
            {
                childLinks.push_back(&link);
            }
        }

        std::sort(childLinks.begin(), childLinks.end(),
                  [](const BTLink* a, const BTLink* b) { return a->sortOrder < b->sortOrder; });

        std::vector<BTNode*> children;
        children.reserve(childLinks.size());
        for (const auto* link : childLinks)
        {
            BTNode* child = findNodeById(link->targetNodeId);
            if (child)
            {
                children.push_back(child);
            }
        }
        return children;
    }

    const BTNode* BTGraph::getParent(uint32_t nodeId) const
    {
        for (const auto& link : links)
        {
            if (link.targetNodeId == nodeId)
            {
                return findNodeById(link.sourceNodeId);
            }
        }
        return nullptr;
    }

    bool isCompositeNode(BTNodeType type)
    {
        return type == BTNodeType::Sequence || type == BTNodeType::Selector || type == BTNodeType::Parallel;
    }

    bool isDecoratorNode(BTNodeType type)
    {
        return type == BTNodeType::Inverter || type == BTNodeType::Repeater ||
               type == BTNodeType::Succeeder || type == BTNodeType::RepeatUntilFail ||
               type == BTNodeType::Cooldown || type == BTNodeType::TimeLimit ||
               type == BTNodeType::BlackboardCondition;
    }

    bool isTaskNode(BTNodeType type)
    {
        return type == BTNodeType::Wait || type == BTNodeType::Log ||
               type == BTNodeType::MoveTo || type == BTNodeType::PlayAnimation ||
               type == BTNodeType::SetBlackboardValue || type == BTNodeType::CheckBlackboardValue ||
               type == BTNodeType::ScriptTask || type == BTNodeType::EnvironmentQuery ||
               type == BTNodeType::LineOfSight || type == BTNodeType::SubTree;
    }

    bool isRootNode(BTNodeType type)
    {
        return type == BTNodeType::Root;
    }

    bool hasOutputPin(BTNodeType type)
    {
        // Tasks (leaves) have no children
        return !isTaskNode(type);
    }

    const char* nodeTypeToString(BTNodeType type)
    {
        switch (type)
        {
        case BTNodeType::Root: return "Root";
        case BTNodeType::Sequence: return "Sequence";
        case BTNodeType::Selector: return "Selector";
        case BTNodeType::Parallel: return "Parallel";
        case BTNodeType::Inverter: return "Inverter";
        case BTNodeType::Repeater: return "Repeater";
        case BTNodeType::Succeeder: return "Succeeder";
        case BTNodeType::RepeatUntilFail: return "RepeatUntilFail";
        case BTNodeType::Cooldown: return "Cooldown";
        case BTNodeType::TimeLimit: return "TimeLimit";
        case BTNodeType::BlackboardCondition: return "BlackboardCondition";
        case BTNodeType::Wait: return "Wait";
        case BTNodeType::Log: return "Log";
        case BTNodeType::MoveTo: return "MoveTo";
        case BTNodeType::PlayAnimation: return "PlayAnimation";
        case BTNodeType::SetBlackboardValue: return "SetBlackboardValue";
        case BTNodeType::CheckBlackboardValue: return "CheckBlackboardValue";
        case BTNodeType::ScriptTask: return "ScriptTask";
        case BTNodeType::EnvironmentQuery: return "EnvironmentQuery";
        case BTNodeType::LineOfSight: return "LineOfSight";
        case BTNodeType::SubTree: return "SubTree";
        default: return "Unknown";
        }
    }

    BTNodeType stringToNodeType(const std::string& str)
    {
        if (str == "Root") return BTNodeType::Root;
        if (str == "Sequence") return BTNodeType::Sequence;
        if (str == "Selector") return BTNodeType::Selector;
        if (str == "Parallel") return BTNodeType::Parallel;
        if (str == "Inverter") return BTNodeType::Inverter;
        if (str == "Repeater") return BTNodeType::Repeater;
        if (str == "Succeeder") return BTNodeType::Succeeder;
        if (str == "RepeatUntilFail") return BTNodeType::RepeatUntilFail;
        if (str == "Cooldown") return BTNodeType::Cooldown;
        if (str == "TimeLimit") return BTNodeType::TimeLimit;
        if (str == "BlackboardCondition") return BTNodeType::BlackboardCondition;
        if (str == "Wait") return BTNodeType::Wait;
        if (str == "Log") return BTNodeType::Log;
        if (str == "MoveTo") return BTNodeType::MoveTo;
        if (str == "PlayAnimation") return BTNodeType::PlayAnimation;
        if (str == "SetBlackboardValue") return BTNodeType::SetBlackboardValue;
        if (str == "CheckBlackboardValue") return BTNodeType::CheckBlackboardValue;
        if (str == "ScriptTask") return BTNodeType::ScriptTask;
        if (str == "EnvironmentQuery") return BTNodeType::EnvironmentQuery;
        if (str == "LineOfSight") return BTNodeType::LineOfSight;
        if (str == "SubTree") return BTNodeType::SubTree;
        return BTNodeType::Sequence;
    }

    const char* blackboardValueTypeToString(BlackboardValueType type)
    {
        switch (type)
        {
        case BlackboardValueType::Float: return "Float";
        case BlackboardValueType::Int: return "Int";
        case BlackboardValueType::Bool: return "Bool";
        case BlackboardValueType::String: return "String";
        case BlackboardValueType::Vec3: return "Vec3";
        case BlackboardValueType::Entity: return "Entity";
        default: return "Float";
        }
    }

    BlackboardValueType stringToBlackboardValueType(const std::string& str)
    {
        if (str == "Float") return BlackboardValueType::Float;
        if (str == "Int") return BlackboardValueType::Int;
        if (str == "Bool") return BlackboardValueType::Bool;
        if (str == "String") return BlackboardValueType::String;
        if (str == "Vec3") return BlackboardValueType::Vec3;
        if (str == "Entity") return BlackboardValueType::Entity;
        return BlackboardValueType::Float;
    }

    const char* compareOpToString(CompareOp op)
    {
        switch (op)
        {
        case CompareOp::Equal: return "==";
        case CompareOp::NotEqual: return "!=";
        case CompareOp::Greater: return ">";
        case CompareOp::Less: return "<";
        case CompareOp::GreaterEqual: return ">=";
        case CompareOp::LessEqual: return "<=";
        default: return "==";
        }
    }

    CompareOp stringToCompareOp(const std::string& str)
    {
        if (str == "==") return CompareOp::Equal;
        if (str == "!=") return CompareOp::NotEqual;
        if (str == ">") return CompareOp::Greater;
        if (str == "<") return CompareOp::Less;
        if (str == ">=") return CompareOp::GreaterEqual;
        if (str == "<=") return CompareOp::LessEqual;
        return CompareOp::Equal;
    }

    const char* abortModeToString(AbortMode mode)
    {
        switch (mode)
        {
        case AbortMode::None: return "None";
        case AbortMode::Self: return "Self";
        case AbortMode::LowerPriority: return "LowerPriority";
        case AbortMode::Both: return "Both";
        default: return "None";
        }
    }

    AbortMode stringToAbortMode(const std::string& str)
    {
        if (str == "None") return AbortMode::None;
        if (str == "Self") return AbortMode::Self;
        if (str == "LowerPriority") return AbortMode::LowerPriority;
        if (str == "Both") return AbortMode::Both;
        return AbortMode::None;
    }

    const char* logLevelToString(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Info: return "Info";
        case LogLevel::Warn: return "Warn";
        case LogLevel::Error: return "Error";
        default: return "Info";
        }
    }

    LogLevel stringToLogLevel(const std::string& str)
    {
        if (str == "Info") return LogLevel::Info;
        if (str == "Warn") return LogLevel::Warn;
        if (str == "Error") return LogLevel::Error;
        return LogLevel::Info;
    }

    const char* parallelPolicyToString(ParallelPolicy policy)
    {
        switch (policy)
        {
        case ParallelPolicy::RequireAll: return "RequireAll";
        case ParallelPolicy::RequireOne: return "RequireOne";
        default: return "RequireAll";
        }
    }

    ParallelPolicy stringToParallelPolicy(const std::string& str)
    {
        if (str == "RequireAll") return ParallelPolicy::RequireAll;
        if (str == "RequireOne") return ParallelPolicy::RequireOne;
        return ParallelPolicy::RequireAll;
    }
}

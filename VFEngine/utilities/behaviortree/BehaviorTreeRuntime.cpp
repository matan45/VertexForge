#include "BehaviorTreeRuntime.hpp"
#include <cmath>

namespace behaviortree
{
    void BehaviorTreeRuntime::init(BehaviorTreeData data, services::EntityHandle entity)
    {
        treeData = std::move(data);
        ownerEntity = entity;
        blackboard.initializeFromGraph(treeData.graph);
        nodeStates.clear();
    }

    BTNodeStatus BehaviorTreeRuntime::tick(float deltaTime, IBTTaskExecutor* executor)
    {
        if (treeData.graph.rootNodeId == 0)
        {
            return BTNodeStatus::Failure;
        }

        return tickNode(treeData.graph.rootNodeId, deltaTime, executor);
    }

    void BehaviorTreeRuntime::reset()
    {
        nodeStates.clear();
        blackboard.initializeFromGraph(treeData.graph);
    }

    void BehaviorTreeRuntime::resetSubtreeState(uint32_t nodeId)
    {
        nodeStates.erase(nodeId);
        auto children = treeData.graph.getChildren(nodeId);
        for (const auto* child : children)
        {
            resetSubtreeState(child->id);
        }
    }

    BTNodeRuntime& BehaviorTreeRuntime::getNodeState(uint32_t nodeId)
    {
        return nodeStates[nodeId];
    }

    BTNodeStatus BehaviorTreeRuntime::tickNode(uint32_t nodeId, float dt, IBTTaskExecutor* executor)
    {
        const BTNode* node = treeData.graph.findNodeById(nodeId);
        if (!node)
        {
            return BTNodeStatus::Failure;
        }

        BTNodeStatus status;

        if (isRootNode(node->type))
        {
            auto children = treeData.graph.getChildren(nodeId);
            if (children.empty())
            {
                status = BTNodeStatus::Failure;
            }
            else
            {
                status = tickNode(children[0]->id, dt, executor);
            }
        }
        else if (isCompositeNode(node->type))
        {
            status = tickComposite(*node, dt, executor);
        }
        else if (isDecoratorNode(node->type))
        {
            status = tickDecorator(*node, dt, executor);
        }
        else
        {
            status = tickTask(*node, dt, executor);
        }

        auto& state = getNodeState(nodeId);
        state.lastStatus = status;
        state.isFirstTick = false;

        return status;
    }

    BTNodeStatus BehaviorTreeRuntime::tickComposite(const BTNode& node, float dt, IBTTaskExecutor* executor)
    {
        auto children = treeData.graph.getChildren(node.id);
        if (children.empty())
        {
            return BTNodeStatus::Failure;
        }

        auto& state = getNodeState(node.id);

        switch (node.type)
        {
        case BTNodeType::Sequence:
        {
            for (int i = state.currentChildIndex; i < static_cast<int>(children.size()); ++i)
            {
                BTNodeStatus childStatus = tickNode(children[i]->id, dt, executor);
                if (childStatus == BTNodeStatus::Running)
                {
                    state.currentChildIndex = i;
                    return BTNodeStatus::Running;
                }
                if (childStatus == BTNodeStatus::Failure)
                {
                    state.currentChildIndex = 0;
                    for (const auto* child : children) resetSubtreeState(child->id);
                    return BTNodeStatus::Failure;
                }
            }
            state.currentChildIndex = 0;
            for (const auto* child : children) resetSubtreeState(child->id);
            return BTNodeStatus::Success;
        }

        case BTNodeType::Selector:
        {
            for (int i = state.currentChildIndex; i < static_cast<int>(children.size()); ++i)
            {
                BTNodeStatus childStatus = tickNode(children[i]->id, dt, executor);
                if (childStatus == BTNodeStatus::Running)
                {
                    state.currentChildIndex = i;
                    return BTNodeStatus::Running;
                }
                if (childStatus == BTNodeStatus::Success)
                {
                    state.currentChildIndex = 0;
                    for (const auto* child : children) resetSubtreeState(child->id);
                    return BTNodeStatus::Success;
                }
            }
            state.currentChildIndex = 0;
            for (const auto* child : children) resetSubtreeState(child->id);
            return BTNodeStatus::Failure;
        }

        case BTNodeType::Parallel:
        {
            ParallelPolicy policy = ParallelPolicy::RequireAll;
            auto policyIt = node.properties.find("policy");
            if (policyIt != node.properties.end() && std::holds_alternative<std::string>(policyIt->second))
            {
                policy = stringToParallelPolicy(std::get<std::string>(policyIt->second));
            }

            int successCount = 0;
            int failureCount = 0;
            bool anyRunning = false;

            for (const auto* child : children)
            {
                BTNodeStatus childStatus = tickNode(child->id, dt, executor);
                switch (childStatus)
                {
                case BTNodeStatus::Success: ++successCount; break;
                case BTNodeStatus::Failure: ++failureCount; break;
                case BTNodeStatus::Running: anyRunning = true; break;
                }
            }

            if (policy == ParallelPolicy::RequireAll)
            {
                if (failureCount > 0) return BTNodeStatus::Failure;
                if (anyRunning) return BTNodeStatus::Running;
                return BTNodeStatus::Success;
            }
            else // RequireOne
            {
                if (successCount > 0) return BTNodeStatus::Success;
                if (anyRunning) return BTNodeStatus::Running;
                return BTNodeStatus::Failure;
            }
        }

        default:
            return BTNodeStatus::Failure;
        }
    }

    BTNodeStatus BehaviorTreeRuntime::tickDecorator(const BTNode& node, float dt, IBTTaskExecutor* executor)
    {
        auto children = treeData.graph.getChildren(node.id);
        auto& state = getNodeState(node.id);

        switch (node.type)
        {
        case BTNodeType::Inverter:
        {
            if (children.empty()) return BTNodeStatus::Failure;
            BTNodeStatus childStatus = tickNode(children[0]->id, dt, executor);
            if (childStatus == BTNodeStatus::Running) return BTNodeStatus::Running;
            return childStatus == BTNodeStatus::Success ? BTNodeStatus::Failure : BTNodeStatus::Success;
        }

        case BTNodeType::Succeeder:
        {
            if (children.empty()) return BTNodeStatus::Success;
            BTNodeStatus childStatus = tickNode(children[0]->id, dt, executor);
            if (childStatus == BTNodeStatus::Running) return BTNodeStatus::Running;
            return BTNodeStatus::Success;
        }

        case BTNodeType::Repeater:
        {
            if (children.empty()) return BTNodeStatus::Failure;

            int maxRepeats = 1;
            auto it = node.properties.find("repeatCount");
            if (it != node.properties.end() && std::holds_alternative<int32_t>(it->second))
            {
                maxRepeats = std::get<int32_t>(it->second);
            }

            BTNodeStatus childStatus = tickNode(children[0]->id, dt, executor);
            if (childStatus == BTNodeStatus::Running) return BTNodeStatus::Running;

            state.repeatCount++;

            resetSubtreeState(children[0]->id);

            if (maxRepeats <= 0 || state.repeatCount < maxRepeats)
            {
                return BTNodeStatus::Running;
            }

            state.repeatCount = 0;
            return childStatus;
        }

        case BTNodeType::RepeatUntilFail:
        {
            if (children.empty()) return BTNodeStatus::Failure;
            BTNodeStatus childStatus = tickNode(children[0]->id, dt, executor);
            if (childStatus == BTNodeStatus::Failure)
            {
                return BTNodeStatus::Success;
            }
            return BTNodeStatus::Running;
        }

        case BTNodeType::Cooldown:
        {
            if (children.empty()) return BTNodeStatus::Failure;

            float cooldownTime = 1.0f;
            auto it = node.properties.find("cooldownTime");
            if (it != node.properties.end() && std::holds_alternative<float>(it->second))
            {
                cooldownTime = std::get<float>(it->second);
            }

            if (state.elapsedTime > 0.0f)
            {
                state.elapsedTime -= dt;
                if (state.elapsedTime > 0.0f)
                {
                    return BTNodeStatus::Failure;
                }
            }

            BTNodeStatus childStatus = tickNode(children[0]->id, dt, executor);
            if (childStatus != BTNodeStatus::Running)
            {
                state.elapsedTime = cooldownTime;
            }
            return childStatus;
        }

        case BTNodeType::TimeLimit:
        {
            if (children.empty()) return BTNodeStatus::Failure;

            float timeLimit = 5.0f;
            auto it = node.properties.find("timeLimit");
            if (it != node.properties.end() && std::holds_alternative<float>(it->second))
            {
                timeLimit = std::get<float>(it->second);
            }

            if (state.isFirstTick)
            {
                state.elapsedTime = 0.0f;
            }

            state.elapsedTime += dt;
            if (state.elapsedTime >= timeLimit)
            {
                state.elapsedTime = 0.0f;
                return BTNodeStatus::Failure;
            }

            BTNodeStatus childStatus = tickNode(children[0]->id, dt, executor);
            if (childStatus != BTNodeStatus::Running)
            {
                state.elapsedTime = 0.0f;
            }
            return childStatus;
        }

        default:
            return BTNodeStatus::Failure;
        }
    }

    template <typename T>
    static BTNodeStatus compareValues(T a, T b, CompareOp op)
    {
        switch (op) {
        case CompareOp::Equal:        return a == b ? BTNodeStatus::Success : BTNodeStatus::Failure;
        case CompareOp::NotEqual:     return a != b ? BTNodeStatus::Success : BTNodeStatus::Failure;
        case CompareOp::Greater:      return a > b ? BTNodeStatus::Success : BTNodeStatus::Failure;
        case CompareOp::Less:         return a < b ? BTNodeStatus::Success : BTNodeStatus::Failure;
        case CompareOp::GreaterEqual: return a >= b ? BTNodeStatus::Success : BTNodeStatus::Failure;
        case CompareOp::LessEqual:    return a <= b ? BTNodeStatus::Success : BTNodeStatus::Failure;
        }
        return BTNodeStatus::Failure;
    }

    static BTNodeStatus compareBlackboardValues(const BlackboardValue& bbVal, const BlackboardValue& compareVal, CompareOp op)
    {
        if (std::holds_alternative<float>(bbVal) && std::holds_alternative<float>(compareVal)) {
            float a = std::get<float>(bbVal), b = std::get<float>(compareVal);
            constexpr float epsilon = 1e-5f;
            if (op == CompareOp::Equal) return std::abs(a - b) < epsilon ? BTNodeStatus::Success : BTNodeStatus::Failure;
            if (op == CompareOp::NotEqual) return std::abs(a - b) >= epsilon ? BTNodeStatus::Success : BTNodeStatus::Failure;
            return compareValues(a, b, op);
        }
        if (std::holds_alternative<int32_t>(bbVal) && std::holds_alternative<int32_t>(compareVal))
            return compareValues(std::get<int32_t>(bbVal), std::get<int32_t>(compareVal), op);
        if (std::holds_alternative<bool>(bbVal) && std::holds_alternative<bool>(compareVal)) {
            if (op == CompareOp::Equal) return std::get<bool>(bbVal) == std::get<bool>(compareVal) ? BTNodeStatus::Success : BTNodeStatus::Failure;
            if (op == CompareOp::NotEqual) return std::get<bool>(bbVal) != std::get<bool>(compareVal) ? BTNodeStatus::Success : BTNodeStatus::Failure;
        }
        if (std::holds_alternative<std::string>(bbVal) && std::holds_alternative<std::string>(compareVal)) {
            if (op == CompareOp::Equal) return std::get<std::string>(bbVal) == std::get<std::string>(compareVal) ? BTNodeStatus::Success : BTNodeStatus::Failure;
            if (op == CompareOp::NotEqual) return std::get<std::string>(bbVal) != std::get<std::string>(compareVal) ? BTNodeStatus::Success : BTNodeStatus::Failure;
        }
        return BTNodeStatus::Failure;
    }

    template <typename T>
    static T getNodeProperty(const BTNode& node, const std::string& key, T defaultVal)
    {
        auto it = node.properties.find(key);
        if (it != node.properties.end() && std::holds_alternative<T>(it->second))
            return std::get<T>(it->second);
        return defaultVal;
    }

    BTNodeStatus BehaviorTreeRuntime::tickTask(const BTNode& node, float dt, IBTTaskExecutor* executor)
    {
        auto& state = getNodeState(node.id);

        switch (node.type)
        {
        case BTNodeType::Wait:
        {
            float duration = getNodeProperty<float>(node, "duration", 1.0f);
            if (state.isFirstTick) state.elapsedTime = 0.0f;
            state.elapsedTime += dt;
            if (state.elapsedTime >= duration) { state.elapsedTime = 0.0f; return BTNodeStatus::Success; }
            return BTNodeStatus::Running;
        }

        case BTNodeType::Log:
        {
            if (!executor) return BTNodeStatus::Failure;
            std::string message = getNodeProperty<std::string>(node, "message", std::string("BT Log"));
            std::string lvlStr = getNodeProperty<std::string>(node, "level", std::string("Info"));
            return executor->executeLog(message, stringToLogLevel(lvlStr));
        }

        case BTNodeType::MoveTo:
        {
            if (!executor) return BTNodeStatus::Failure;
            std::string targetKey = getNodeProperty<std::string>(node, "targetKey", std::string("target"));
            float arrivalDistance = getNodeProperty<float>(node, "arrivalDistance", 0.5f);
            return executor->executeMoveTo(ownerEntity, targetKey, arrivalDistance, blackboard, state.isFirstTick);
        }

        case BTNodeType::PlayAnimation:
        {
            if (!executor) return BTNodeStatus::Failure;
            std::string stateName = getNodeProperty<std::string>(node, "stateName", std::string{});
            bool waitForCompletion = getNodeProperty<bool>(node, "waitForCompletion", false);
            return executor->executePlayAnimation(ownerEntity, stateName, waitForCompletion);
        }

        case BTNodeType::SetBlackboardValue:
        {
            std::string key = getNodeProperty<std::string>(node, "key", std::string{});
            if (key.empty()) return BTNodeStatus::Failure;
            auto valIt = node.properties.find("value");
            if (valIt != node.properties.end()) blackboard.set(key, valIt->second);
            return BTNodeStatus::Success;
        }

        case BTNodeType::CheckBlackboardValue:
        {
            std::string key = getNodeProperty<std::string>(node, "key", std::string{});
            if (key.empty() || !blackboard.has(key)) return BTNodeStatus::Failure;

            CompareOp op = stringToCompareOp(getNodeProperty<std::string>(node, "compareOp", std::string("Equal")));
            auto compareValIt = node.properties.find("compareValue");
            if (compareValIt == node.properties.end()) return BTNodeStatus::Failure;
            return compareBlackboardValues(blackboard.get(key), compareValIt->second, op);
        }

        case BTNodeType::ScriptTask:
        {
            if (!executor) return BTNodeStatus::Failure;
            return executor->executeScriptTask(ownerEntity, node.scriptPath, node.scriptClassName, blackboard, dt);
        }

        case BTNodeType::EnvironmentQuery:
        {
            if (!executor) return BTNodeStatus::Failure;
            std::string queryName = getNodeProperty<std::string>(node, "queryName", std::string{});
            std::string resultKey = getNodeProperty<std::string>(node, "resultKey", std::string("eqsResult"));
            return executor->executeEnvironmentQuery(ownerEntity, queryName, resultKey, blackboard, state.isFirstTick);
        }

        case BTNodeType::LineOfSight:
        {
            if (!executor) return BTNodeStatus::Failure;
            std::string targetKey = getNodeProperty<std::string>(node, "targetKey", std::string("target"));
            float maxDistance = getNodeProperty<float>(node, "maxDistance", 50.0f);
            float eyeOffset = getNodeProperty<float>(node, "eyeOffset", 1.6f);
            return executor->executeLineOfSight(ownerEntity, targetKey, maxDistance, eyeOffset, blackboard);
        }

        default:
            return BTNodeStatus::Failure;
        }
    }
}

#include "BehaviorTreeRuntime.hpp"

namespace behaviortree
{
    void BehaviorTreeRuntime::init(const BehaviorTreeData& data, services::EntityHandle entity)
    {
        treeData = &data;
        ownerEntity = entity;
        blackboard.initializeFromGraph(data.graph);
        nodeStates.clear();
    }

    BTNodeStatus BehaviorTreeRuntime::tick(float deltaTime, IBTTaskExecutor* executor)
    {
        if (!treeData || treeData->graph.rootNodeId == 0)
        {
            return BTNodeStatus::Failure;
        }

        return tickNode(treeData->graph.rootNodeId, deltaTime, executor);
    }

    void BehaviorTreeRuntime::reset()
    {
        nodeStates.clear();
        if (treeData)
        {
            blackboard.initializeFromGraph(treeData->graph);
        }
    }

    BTNodeRuntime& BehaviorTreeRuntime::getNodeState(uint32_t nodeId)
    {
        return nodeStates[nodeId];
    }

    BTNodeStatus BehaviorTreeRuntime::tickNode(uint32_t nodeId, float dt, IBTTaskExecutor* executor)
    {
        const BTNode* node = treeData->graph.findNodeById(nodeId);
        if (!node)
        {
            return BTNodeStatus::Failure;
        }

        BTNodeStatus status;

        if (isRootNode(node->type))
        {
            // Root just ticks its single child
            auto children = treeData->graph.getChildren(nodeId);
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
        auto children = treeData->graph.getChildren(node.id);
        if (children.empty())
        {
            return BTNodeStatus::Failure;
        }

        auto& state = getNodeState(node.id);

        switch (node.type)
        {
        case BTNodeType::Sequence:
        {
            // Sequence: run children left-to-right, fail on first failure
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
                    return BTNodeStatus::Failure;
                }
            }
            state.currentChildIndex = 0;
            return BTNodeStatus::Success;
        }

        case BTNodeType::Selector:
        {
            // Selector: run children left-to-right, succeed on first success
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
                    return BTNodeStatus::Success;
                }
            }
            state.currentChildIndex = 0;
            return BTNodeStatus::Failure;
        }

        case BTNodeType::Parallel:
        {
            // Parallel: tick all children, use policy to determine result
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
        auto children = treeData->graph.getChildren(node.id);
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
            if (maxRepeats <= 0 || state.repeatCount < maxRepeats)
            {
                return BTNodeStatus::Running; // Keep repeating
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
                return BTNodeStatus::Success; // Stop repeating, return success
            }
            return BTNodeStatus::Running; // Keep repeating
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
                    return BTNodeStatus::Failure; // Still on cooldown
                }
            }

            BTNodeStatus childStatus = tickNode(children[0]->id, dt, executor);
            if (childStatus != BTNodeStatus::Running)
            {
                state.elapsedTime = cooldownTime; // Start cooldown
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
                return BTNodeStatus::Failure; // Time exceeded
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

    BTNodeStatus BehaviorTreeRuntime::tickTask(const BTNode& node, float dt, IBTTaskExecutor* executor)
    {
        auto& state = getNodeState(node.id);

        switch (node.type)
        {
        case BTNodeType::Wait:
        {
            float duration = 1.0f;
            auto it = node.properties.find("duration");
            if (it != node.properties.end() && std::holds_alternative<float>(it->second))
            {
                duration = std::get<float>(it->second);
            }

            if (state.isFirstTick)
            {
                state.elapsedTime = 0.0f;
            }

            state.elapsedTime += dt;
            if (state.elapsedTime >= duration)
            {
                state.elapsedTime = 0.0f;
                return BTNodeStatus::Success;
            }
            return BTNodeStatus::Running;
        }

        case BTNodeType::Log:
        {
            if (!executor) return BTNodeStatus::Failure;

            std::string message = "BT Log";
            auto msgIt = node.properties.find("message");
            if (msgIt != node.properties.end() && std::holds_alternative<std::string>(msgIt->second))
            {
                message = std::get<std::string>(msgIt->second);
            }

            LogLevel level = LogLevel::Info;
            auto lvlIt = node.properties.find("level");
            if (lvlIt != node.properties.end() && std::holds_alternative<std::string>(lvlIt->second))
            {
                level = stringToLogLevel(std::get<std::string>(lvlIt->second));
            }

            return executor->executeLog(message, level);
        }

        case BTNodeType::MoveTo:
        {
            if (!executor) return BTNodeStatus::Failure;

            std::string targetKey = "target";
            auto keyIt = node.properties.find("targetKey");
            if (keyIt != node.properties.end() && std::holds_alternative<std::string>(keyIt->second))
            {
                targetKey = std::get<std::string>(keyIt->second);
            }

            float arrivalDistance = 0.5f;
            auto distIt = node.properties.find("arrivalDistance");
            if (distIt != node.properties.end() && std::holds_alternative<float>(distIt->second))
            {
                arrivalDistance = std::get<float>(distIt->second);
            }

            return executor->executeMoveTo(ownerEntity, targetKey, arrivalDistance, blackboard);
        }

        case BTNodeType::PlayAnimation:
        {
            if (!executor) return BTNodeStatus::Failure;

            std::string stateName;
            auto nameIt = node.properties.find("stateName");
            if (nameIt != node.properties.end() && std::holds_alternative<std::string>(nameIt->second))
            {
                stateName = std::get<std::string>(nameIt->second);
            }

            bool waitForCompletion = false;
            auto waitIt = node.properties.find("waitForCompletion");
            if (waitIt != node.properties.end() && std::holds_alternative<bool>(waitIt->second))
            {
                waitForCompletion = std::get<bool>(waitIt->second);
            }

            return executor->executePlayAnimation(ownerEntity, stateName, waitForCompletion);
        }

        case BTNodeType::SetBlackboardValue:
        {
            std::string key;
            auto keyIt = node.properties.find("key");
            if (keyIt != node.properties.end() && std::holds_alternative<std::string>(keyIt->second))
            {
                key = std::get<std::string>(keyIt->second);
            }

            if (key.empty()) return BTNodeStatus::Failure;

            auto valIt = node.properties.find("value");
            if (valIt != node.properties.end())
            {
                blackboard.set(key, valIt->second);
            }
            return BTNodeStatus::Success;
        }

        case BTNodeType::CheckBlackboardValue:
        {
            std::string key;
            auto keyIt = node.properties.find("key");
            if (keyIt != node.properties.end() && std::holds_alternative<std::string>(keyIt->second))
            {
                key = std::get<std::string>(keyIt->second);
            }

            if (key.empty() || !blackboard.has(key)) return BTNodeStatus::Failure;

            CompareOp op = CompareOp::Equal;
            auto opIt = node.properties.find("compareOp");
            if (opIt != node.properties.end() && std::holds_alternative<std::string>(opIt->second))
            {
                op = stringToCompareOp(std::get<std::string>(opIt->second));
            }

            BlackboardValue bbVal = blackboard.get(key);
            auto compareValIt = node.properties.find("compareValue");
            if (compareValIt == node.properties.end()) return BTNodeStatus::Failure;

            const BlackboardValue& compareVal = compareValIt->second;

            // Compare floats
            if (std::holds_alternative<float>(bbVal) && std::holds_alternative<float>(compareVal))
            {
                float a = std::get<float>(bbVal);
                float b = std::get<float>(compareVal);
                switch (op)
                {
                case CompareOp::Equal: return a == b ? BTNodeStatus::Success : BTNodeStatus::Failure;
                case CompareOp::NotEqual: return a != b ? BTNodeStatus::Success : BTNodeStatus::Failure;
                case CompareOp::Greater: return a > b ? BTNodeStatus::Success : BTNodeStatus::Failure;
                case CompareOp::Less: return a < b ? BTNodeStatus::Success : BTNodeStatus::Failure;
                case CompareOp::GreaterEqual: return a >= b ? BTNodeStatus::Success : BTNodeStatus::Failure;
                case CompareOp::LessEqual: return a <= b ? BTNodeStatus::Success : BTNodeStatus::Failure;
                }
            }

            // Compare ints
            if (std::holds_alternative<int32_t>(bbVal) && std::holds_alternative<int32_t>(compareVal))
            {
                int32_t a = std::get<int32_t>(bbVal);
                int32_t b = std::get<int32_t>(compareVal);
                switch (op)
                {
                case CompareOp::Equal: return a == b ? BTNodeStatus::Success : BTNodeStatus::Failure;
                case CompareOp::NotEqual: return a != b ? BTNodeStatus::Success : BTNodeStatus::Failure;
                case CompareOp::Greater: return a > b ? BTNodeStatus::Success : BTNodeStatus::Failure;
                case CompareOp::Less: return a < b ? BTNodeStatus::Success : BTNodeStatus::Failure;
                case CompareOp::GreaterEqual: return a >= b ? BTNodeStatus::Success : BTNodeStatus::Failure;
                case CompareOp::LessEqual: return a <= b ? BTNodeStatus::Success : BTNodeStatus::Failure;
                }
            }

            // Compare bools (only Equal/NotEqual)
            if (std::holds_alternative<bool>(bbVal) && std::holds_alternative<bool>(compareVal))
            {
                bool a = std::get<bool>(bbVal);
                bool b = std::get<bool>(compareVal);
                if (op == CompareOp::Equal) return a == b ? BTNodeStatus::Success : BTNodeStatus::Failure;
                if (op == CompareOp::NotEqual) return a != b ? BTNodeStatus::Success : BTNodeStatus::Failure;
            }

            // Compare strings (only Equal/NotEqual)
            if (std::holds_alternative<std::string>(bbVal) && std::holds_alternative<std::string>(compareVal))
            {
                const auto& a = std::get<std::string>(bbVal);
                const auto& b = std::get<std::string>(compareVal);
                if (op == CompareOp::Equal) return a == b ? BTNodeStatus::Success : BTNodeStatus::Failure;
                if (op == CompareOp::NotEqual) return a != b ? BTNodeStatus::Success : BTNodeStatus::Failure;
            }

            return BTNodeStatus::Failure;
        }

        case BTNodeType::ScriptTask:
        {
            if (!executor) return BTNodeStatus::Failure;
            return executor->executeScriptTask(ownerEntity, node.scriptPath, node.scriptClassName,
                                                blackboard, dt);
        }

        default:
            return BTNodeStatus::Failure;
        }
    }
}

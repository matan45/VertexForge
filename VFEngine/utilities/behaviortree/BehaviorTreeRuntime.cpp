#include "BehaviorTreeRuntime.hpp"
#include "BehaviorTreeDynamic.hpp"
#include "../print/Log.hpp"
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <string>

namespace behaviortree
{
    namespace
    {
        constexpr int kMaxNestingDepth = 8;             // matches static SubTree expansion maxDepth
        constexpr std::size_t kMaxExecutionEvents = 128; // bounded debugger history ring
        constexpr std::size_t kMaxAbortRecords = 32;

        std::string normalizeRuntimePath(std::string path)
        {
            std::replace(path.begin(), path.end(), '\\', '/');
            return path;
        }
    }

    void BehaviorTreeRuntime::init(std::shared_ptr<const BehaviorTreeData> data, services::EntityHandle entity)
    {
        treeData = std::move(data);
        ownerEntity = entity;
        blackboard.clear();
        if (treeData)
        {
            blackboard.initializeFromGraph(treeData->graph);
        }
        nodeStates.clear();
        serviceStates.clear();
        conditionCache.clear();

        // VK-1457: per-tree state is rebuilt on (re-)init; dynamic-subtree wiring (resolver/injections/
        // nesting context/debugRecording) is configuration set separately and must survive a hot-reload.
        nestedRuntimes.clear();
        loggedDynamicErrors.clear();
        clearDebugHistory();
    }

    BTNodeStatus BehaviorTreeRuntime::tick(float deltaTime, IBTTaskExecutor* executor)
    {
        if (!treeData || treeData->graph.rootNodeId == 0)
        {
            return BTNodeStatus::Failure;
        }

        if (debugRecording)
        {
            ++recordTickIndex;
            activeDynamicSubtreePath.clear(); // re-derived this tick if a DynamicSubTree runs
        }

        // Mark all services unvisited; tickService re-marks the ones reached this frame.
        for (auto& [id, svc] : serviceStates)
        {
            svc.tickedThisFrame = false;
        }

        BTNodeStatus status = tickNode(treeData->graph.rootNodeId, deltaTime, executor);

        // Any service not visited this frame left the active path -> fire onServiceEnd and drop it.
        endInactiveServices(executor);

        return status;
    }

    void BehaviorTreeRuntime::reset()
    {
        nodeStates.clear();
        serviceStates.clear();
        conditionCache.clear();
        // VK-1457: drop nested dynamic subtrees (they reference tree data being reset).
        nestedRuntimes.clear();
        loggedDynamicErrors.clear();
        clearDebugHistory();
        if (treeData)
        {
            blackboard.initializeFromGraph(treeData->graph);
        }
    }

    void BehaviorTreeRuntime::resetSubtreeState(uint32_t nodeId)
    {
        nodeStates.erase(nodeId);
        // VK-1457: only reached for already-terminal subtrees (a completing composite/repeater resets its
        // children), by which point a DynamicSubTree has already torn its nested runtime down. Drop any
        // lingering nested defensively (no executor here — see abortAll's orphan sweep for the full flush).
        nestedRuntimes.erase(nodeId);
        auto children = treeData->graph.getChildren(nodeId);
        for (const auto* child : children)
        {
            resetSubtreeState(child->id);
        }
    }

    void BehaviorTreeRuntime::abortSubtree(uint32_t nodeId, IBTTaskExecutor* executor)
    {
        auto it = nodeStates.find(nodeId);
        if (it != nodeStates.end() && it->second.lastStatus == BTNodeStatus::Running && executor)
        {
            const BTNode* node = treeData->graph.findNodeById(nodeId);
            if (node && isTaskNode(node->type))
            {
                executor->onAbort(ownerEntity, *node);
                recordAbort(nodeId, "aborted");
            }
        }
        // VK-1457: a DynamicSubTree leaf owns a nested runtime — abort it too so in-flight nav/EQS/scripts
        // inside the subtree are cancelled and its services fire onServiceEnd.
        teardownNested(nodeId, executor);
        nodeStates.erase(nodeId);
        auto children = treeData->graph.getChildren(nodeId);
        for (const auto* child : children)
        {
            abortSubtree(child->id, executor);
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

    bool BehaviorTreeRuntime::evaluateCondition(const BTNode& node) const
    {
        std::string key = getNodeProperty<std::string>(node, "key", std::string{});
        if (key.empty() || !blackboard.has(key)) return false;

        CompareOp op = stringToCompareOp(getNodeProperty<std::string>(node, "compareOp", std::string("==")));
        auto compareValIt = node.properties.find("compareValue");
        if (compareValIt == node.properties.end()) return false;

        return compareBlackboardValues(blackboard.get(key), compareValIt->second, op) == BTNodeStatus::Success;
    }

    bool BehaviorTreeRuntime::observeCondition(const BTNode& node)
    {
        // evaluateCondition depends only on immutable node props + the watched key's presence/value,
        // and every blackboard change bumps that key's version, so memoizing by version is exact.
        const uint64_t version = blackboard.getVersion(getNodeProperty<std::string>(node, "key", std::string{}));

        auto it = conditionCache.find(node.id);
        if (it != conditionCache.end() && it->second.version == version)
        {
            return it->second.result;
        }

        const bool result = evaluateCondition(node);
        conditionCache[node.id] = ConditionCacheEntry{version, result};
        return result;
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
        else if (isServiceNode(node->type))
        {
            status = tickService(*node, dt, executor);
        }
        else
        {
            status = tickTask(*node, dt, executor);
        }

        auto& state = getNodeState(nodeId);

        // VK-1457 debugger: record task-leaf Enter/Exit transitions (gated; no-op unless recording).
        if (debugRecording && isTaskNode(node->type))
        {
            const bool wasRunning = state.lastStatus == BTNodeStatus::Running;
            const bool nowRunning = status == BTNodeStatus::Running;
            if (!wasRunning && nowRunning)
            {
                recordExecutionEvent(BTEventType::Enter, nodeId, status);
            }
            else if (wasRunning && !nowRunning)
            {
                recordExecutionEvent(BTEventType::Exit, nodeId, status);
            }
        }

        state.lastStatus = status;
        state.isFirstTick = false;
        if (status != BTNodeStatus::Running)
        {
            state.lastCompletedStatus = status; // "last result" column for the debugger
        }

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
            // Observer aborts: a higher-priority BlackboardCondition sibling whose
            // condition now passes preempts the running lower-priority branch
            if (state.currentChildIndex > 0)
            {
                for (int i = 0; i < state.currentChildIndex; ++i)
                {
                    const BTNode* candidate = children[i];
                    if (candidate->type != BTNodeType::BlackboardCondition) continue;

                    AbortMode mode = stringToAbortMode(
                        getNodeProperty<std::string>(*candidate, "abortMode", std::string("None")));
                    if (mode != AbortMode::LowerPriority && mode != AbortMode::Both) continue;
                    if (!observeCondition(*candidate)) continue;

                    abortSubtree(children[state.currentChildIndex]->id, executor);
                    state.currentChildIndex = i;
                    break;
                }
            }

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

        case BTNodeType::BlackboardCondition:
        {
            if (children.empty()) return BTNodeStatus::Failure;

            AbortMode mode = stringToAbortMode(
                getNodeProperty<std::string>(node, "abortMode", std::string("None")));
            bool observesSelf = mode == AbortMode::Self || mode == AbortMode::Both;

            auto childIt = nodeStates.find(children[0]->id);
            bool childRunning = childIt != nodeStates.end() &&
                                childIt->second.lastStatus == BTNodeStatus::Running;

            // Once entered, only Self/Both modes keep observing the condition
            if (childRunning && !observesSelf)
            {
                return tickNode(children[0]->id, dt, executor);
            }

            if (!observeCondition(node))
            {
                if (childRunning)
                {
                    abortSubtree(children[0]->id, executor);
                }
                return BTNodeStatus::Failure;
            }

            return tickNode(children[0]->id, dt, executor);
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

    static uint64_t splitmix64(uint64_t x)
    {
        x += 0x9E3779B97F4A7C15ULL;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
        return x ^ (x >> 31);
    }

    float BehaviorTreeRuntime::computeServiceInterval(const BTNode& node, uint32_t fireCount) const
    {
        constexpr float minInterval = 0.001f;
        const float base = getNodeProperty<float>(node, "interval", 0.5f);
        const float deviation = getNodeProperty<float>(node, "randomDeviation", 0.0f);

        if (deviation <= 0.0f)
        {
            return base < minInterval ? minInterval : base;
        }

        // Reproducible factor in [-1, 1) from (entity, node, fireCount): deterministic across runs.
        uint64_t seed = splitmix64(ownerEntity.id);
        seed = splitmix64(seed ^ (static_cast<uint64_t>(node.id) + 0x9E3779B97F4A7C15ULL));
        seed = splitmix64(seed ^ static_cast<uint64_t>(fireCount));
        const double unit = static_cast<double>(seed >> 11) * (1.0 / 9007199254740992.0); // [0,1)
        const float factor = static_cast<float>(unit) * 2.0f - 1.0f;

        const float interval = base + deviation * factor;
        return interval < minInterval ? minInterval : interval;
    }

    BTNodeStatus BehaviorTreeRuntime::tickService(const BTNode& node, float dt, IBTTaskExecutor* executor)
    {
        auto children = treeData->graph.getChildren(node.id);
        if (children.empty())
        {
            return BTNodeStatus::Failure;
        }

        const bool newlyActive = serviceStates.find(node.id) == serviceStates.end();
        ServiceState& svc = serviceStates[node.id];
        svc.tickedThisFrame = true;

        if (newlyActive)
        {
            svc.accumulator = 0.0f;
            svc.fireCount = 0;
            svc.currentInterval = computeServiceInterval(node, 0);
            if (executor)
            {
                executor->onServiceStart(ownerEntity, node, blackboard);
            }

            const bool runOnActivation = getNodeProperty<bool>(node, "runOnActivation", false);
            if (runOnActivation)
            {
                if (executor)
                {
                    executor->onServiceTick(ownerEntity, node, blackboard, dt);
                }
                recordExecutionEvent(BTEventType::ServiceFire, node.id, BTNodeStatus::Running);
                ++svc.fireCount;
                svc.currentInterval = computeServiceInterval(node, svc.fireCount);
            }
        }

        // The activation frame accumulates too, so the first scheduled fire lands one interval later.
        svc.accumulator += dt;
        while (svc.currentInterval > 0.0f && svc.accumulator >= svc.currentInterval)
        {
            svc.accumulator -= svc.currentInterval;
            if (executor)
            {
                executor->onServiceTick(ownerEntity, node, blackboard, dt);
            }
            recordExecutionEvent(BTEventType::ServiceFire, node.id, BTNodeStatus::Running);
            ++svc.fireCount;
            svc.currentInterval = computeServiceInterval(node, svc.fireCount);
        }

        // Passthrough: service runs BEFORE its child, so a sensing service above a Selector feeds
        // the reactive condition in the same frame.
        return tickNode(children[0]->id, dt, executor);
    }

    void BehaviorTreeRuntime::endInactiveServices(IBTTaskExecutor* executor)
    {
        std::vector<uint32_t> ended;
        for (const auto& [id, svc] : serviceStates)
        {
            if (!svc.tickedThisFrame)
            {
                ended.push_back(id);
            }
        }
        std::sort(ended.begin(), ended.end()); // deterministic onServiceEnd order

        for (uint32_t id : ended)
        {
            if (executor && treeData)
            {
                const BTNode* node = treeData->graph.findNodeById(id);
                if (node)
                {
                    executor->onServiceEnd(ownerEntity, *node, blackboard);
                }
            }
            serviceStates.erase(id);
        }
    }

    void BehaviorTreeRuntime::endAllServices(IBTTaskExecutor* executor)
    {
        std::vector<uint32_t> ids;
        ids.reserve(serviceStates.size());
        for (const auto& [id, svc] : serviceStates)
        {
            ids.push_back(id);
        }
        std::sort(ids.begin(), ids.end());

        for (uint32_t id : ids)
        {
            if (executor && treeData)
            {
                const BTNode* node = treeData->graph.findNodeById(id);
                if (node)
                {
                    executor->onServiceEnd(ownerEntity, *node, blackboard);
                }
            }
        }
        serviceStates.clear();

        // VK-1457: nested dynamic subtrees run their own services — flush them when this runtime stops
        // being ticked (detach/disable/stop). Nested runtimes are kept (not dropped) so a re-enable resumes.
        for (auto& [id, nested] : nestedRuntimes)
        {
            if (nested.runtime)
            {
                nested.runtime->endAllServices(executor);
            }
        }
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

        case BTNodeType::DynamicSubTree:
        {
            return tickDynamicSubTree(node, dt, executor);
        }

        default:
            return BTNodeStatus::Failure;
        }
    }

    BTNodeStatus BehaviorTreeRuntime::tickDynamicSubTree(const BTNode& node, float dt, IBTTaskExecutor* executor)
    {
        // 1. Resolve the target tree: blackboard[selectionKey] > injections[injectionTag] > defaultTreePath.
        const std::string path = resolveDynamicSubtreePath(node, blackboard, *injections);
        if (path.empty())
        {
            teardownNested(node.id, executor);
            return BTNodeStatus::Failure;
        }

        std::shared_ptr<const BehaviorTreeData> data;
        if (treeResolver)
        {
            data = treeResolver(path);
        }

        // 2. Rebuild the nested runtime when there is none, the path changed (runtime swap), or the
        //    resolved data pointer changed (asset hot-reload).
        auto it = nestedRuntimes.find(node.id);
        bool needBuild = (it == nestedRuntimes.end());
        if (!needBuild)
        {
            if (it->second.path != path) needBuild = true;
            else if (data && it->second.data.get() != data.get()) needBuild = true;
        }

        if (needBuild)
        {
            teardownNested(node.id, executor); // drop the stale nested (aborts its in-flight work)
            if (!data)
            {
                vfLogWarning("BT DynamicSubTree: could not resolve tree '{}' (node {})", path, node.id);
                return BTNodeStatus::Failure;
            }

            const std::string norm = normalizeRuntimePath(path);
            if (nestingDepth + 1 > kMaxNestingDepth)
            {
                if (loggedDynamicErrors.insert(node.id).second)
                {
                    vfLogError("BT DynamicSubTree: nesting depth limit ({}) exceeded at '{}'",
                               kMaxNestingDepth, path);
                }
                return BTNodeStatus::Failure;
            }
            for (const auto& ancestor : ancestorPaths)
            {
                if (ancestor == norm)
                {
                    if (loggedDynamicErrors.insert(node.id).second)
                    {
                        vfLogError("BT DynamicSubTree: cyclic reference to '{}' rejected", path);
                    }
                    return BTNodeStatus::Failure;
                }
            }

            NestedSubtree nested;
            nested.runtime = std::make_unique<BehaviorTreeRuntime>();
            nested.path = path;
            nested.data = data;
            nested.runtime->init(data, ownerEntity);
            nested.runtime->setTreeResolver(treeResolver);
            nested.runtime->injections = injections; // share the live injection map down the chain
            std::vector<std::string> childAncestors = ancestorPaths;
            childAncestors.push_back(norm);
            nested.runtime->setNestingContext(nestingDepth + 1, std::move(childAncestors));

            it = nestedRuntimes.emplace(node.id, std::move(nested)).first;
            loggedDynamicErrors.erase(node.id); // a fresh successful build clears any prior error latch
        }

        BehaviorTreeRuntime& nested = *it->second.runtime;

        // 3. Copy IN (parent -> child), tick with the SAME executor, copy OUT (child -> parent).
        applyMappingsIn(node.blackboardMappings, blackboard, nested.getBlackboard());
        const BTNodeStatus status = nested.tick(dt, executor);
        applyMappingsOut(node.blackboardMappings, blackboard, nested.getBlackboard());

        if (debugRecording)
        {
            activeDynamicSubtreePath = path;
        }

        // 4. On terminal, drop the nested so the next entry re-selects (and re-copies IN) fresh.
        if (status != BTNodeStatus::Running)
        {
            it->second.runtime->endAllServices(executor);
            nestedRuntimes.erase(it);
        }
        return status;
    }

    void BehaviorTreeRuntime::teardownNested(uint32_t nodeId, IBTTaskExecutor* executor)
    {
        auto it = nestedRuntimes.find(nodeId);
        if (it == nestedRuntimes.end()) return;
        if (it->second.runtime)
        {
            it->second.runtime->abortAll(executor);
        }
        nestedRuntimes.erase(it);
    }

    void BehaviorTreeRuntime::abortAll(IBTTaskExecutor* executor)
    {
        if (treeData && treeData->graph.rootNodeId != 0)
        {
            abortSubtree(treeData->graph.rootNodeId, executor); // fires onAbort + tears down reached nested
        }
        // Orphan sweep: nested not reached from the root (e.g. a Parallel that never aborts a running
        // sibling) still gets aborted + dropped so no in-flight work leaks.
        for (auto& [id, nested] : nestedRuntimes)
        {
            if (nested.runtime)
            {
                nested.runtime->abortAll(executor);
            }
        }
        nestedRuntimes.clear();
        endAllServices(executor);
    }

    void BehaviorTreeRuntime::setDynamicInjection(const std::string& tag, const std::string& path)
    {
        if (tag.empty()) return;
        (*injections)[tag] = path;
    }

    void BehaviorTreeRuntime::clearDynamicInjection(const std::string& tag)
    {
        injections->erase(tag);
    }

    void BehaviorTreeRuntime::setNestingContext(int depth, std::vector<std::string> ancestors)
    {
        nestingDepth = depth;
        ancestorPaths.clear();
        ancestorPaths.reserve(ancestors.size());
        for (auto& ancestor : ancestors)
        {
            ancestorPaths.push_back(normalizeRuntimePath(std::move(ancestor)));
        }
    }

    void BehaviorTreeRuntime::clearDebugHistory()
    {
        abortRecords.clear();
        executionEvents.clear();
        activeDynamicSubtreePath.clear();
        recordTickIndex = 0;
    }

    void BehaviorTreeRuntime::recordExecutionEvent(BTEventType type, uint32_t nodeId, BTNodeStatus status)
    {
        if (!debugRecording) return;
        executionEvents.push_back(BTExecutionEvent{recordTickIndex, nodeId, type, status});
        if (executionEvents.size() > kMaxExecutionEvents)
        {
            executionEvents.erase(executionEvents.begin());
        }
    }

    void BehaviorTreeRuntime::recordAbort(uint32_t nodeId, const std::string& reason)
    {
        if (!debugRecording) return;
        abortRecords.push_back(BTAbortRecord{nodeId, recordTickIndex, reason});
        if (abortRecords.size() > kMaxAbortRecords)
        {
            abortRecords.erase(abortRecords.begin());
        }
        recordExecutionEvent(BTEventType::Abort, nodeId, BTNodeStatus::Failure);
    }
}

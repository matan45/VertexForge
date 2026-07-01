#pragma once

#include "BehaviorTreeTypes.hpp"
#include "Blackboard.hpp"
#include <unordered_map>
#include <memory>

namespace behaviortree
{
    struct BTNodeRuntime
    {
        BTNodeStatus lastStatus = BTNodeStatus::Failure;
        int currentChildIndex = 0;
        float elapsedTime = 0.0f;
        int repeatCount = 0;
        bool isFirstTick = true;
    };

    // Live state for a Service node (VK-1456). Kept SEPARATE from BTNodeRuntime/nodeStates:
    // service lifetime is owned by the end-of-tick "active-set diff", not by abort/reset erases.
    struct ServiceState
    {
        bool tickedThisFrame = false;
        float accumulator = 0.0f;
        float currentInterval = 0.0f;
        uint32_t fireCount = 0;
    };

    // Memoized result of a BlackboardCondition, keyed by the watched key's blackboard version.
    // Presence in the map == validity; the version compare self-heals stale entries.
    struct ConditionCacheEntry
    {
        uint64_t version = 0;
        bool result = false;
    };

    class IBTTaskExecutor
    {
    public:
        virtual ~IBTTaskExecutor() = default;

        virtual BTNodeStatus executeMoveTo(services::EntityHandle entity,
                                           const std::string& targetKey,
                                           float arrivalDistance,
                                           Blackboard& blackboard,
                                           bool isFirstTick) = 0;

        virtual BTNodeStatus executePlayAnimation(services::EntityHandle entity,
                                                   const std::string& stateName,
                                                   bool waitForCompletion) = 0;

        virtual BTNodeStatus executeScriptTask(services::EntityHandle entity,
                                                const std::string& scriptPath,
                                                const std::string& className,
                                                Blackboard& blackboard,
                                                float deltaTime) = 0;

        virtual BTNodeStatus executeLog(const std::string& message, LogLevel level) = 0;

        virtual BTNodeStatus executeEnvironmentQuery(
            services::EntityHandle entity,
            const std::string& queryName,
            const std::string& resultKey,
            Blackboard& blackboard,
            bool isFirstTick) = 0;

        virtual BTNodeStatus executeLineOfSight(services::EntityHandle entity,
                                                 const std::string& targetKey,
                                                 float maxDistance,
                                                 float eyeOffset,
                                                 Blackboard& blackboard) = 0;

        // Called when a Running task node is aborted (observer abort / self abort)
        // so the executor can cancel in-flight side effects (nav requests, EQS queries, scripts)
        virtual void onAbort(services::EntityHandle entity, const BTNode& node)
        {
            (void)entity;
            (void)node;
        }

        // Service lifecycle hooks (VK-1456). Non-pure so existing executors/mocks keep compiling.
        // onServiceStart: branch containing this Service just became active.
        // onServiceTick:  a scheduled service fire (interval elapsed, or run-on-activation).
        // onServiceEnd:   branch left the active path (abort/reset/detach) — cancel in-flight async.
        virtual void onServiceStart(services::EntityHandle entity, const BTNode& node, Blackboard& blackboard)
        {
            (void)entity;
            (void)node;
            (void)blackboard;
        }

        virtual void onServiceTick(services::EntityHandle entity, const BTNode& node, Blackboard& blackboard, float deltaTime)
        {
            (void)entity;
            (void)node;
            (void)blackboard;
            (void)deltaTime;
        }

        virtual void onServiceEnd(services::EntityHandle entity, const BTNode& node, Blackboard& blackboard)
        {
            (void)entity;
            (void)node;
            (void)blackboard;
        }
    };

    class BehaviorTreeRuntime
    {
    private:
        // Immutable tree data, shared between all runtimes attached to the same asset path.
        // All mutable per-agent state lives in nodeStates/blackboard.
        std::shared_ptr<const BehaviorTreeData> treeData;
        services::EntityHandle ownerEntity;
        Blackboard blackboard;
        std::unordered_map<uint32_t, BTNodeRuntime> nodeStates;

        // VK-1456 reactivity state. serviceStates is owned by the end-of-tick diff (endInactiveServices),
        // not by abort/reset. conditionCache is a pure optimization self-healed by blackboard versions.
        std::unordered_map<uint32_t, ServiceState> serviceStates;
        std::unordered_map<uint32_t, ConditionCacheEntry> conditionCache;
    public:
        void init(std::shared_ptr<const BehaviorTreeData> data, services::EntityHandle entity);
        BTNodeStatus tick(float deltaTime, IBTTaskExecutor* executor);
        void reset();

        // Fire onServiceEnd for every currently-active service and clear them. Call from the executor
        // (e.g. adapter detach/disable/stopAll/reload) before a runtime stops being ticked, since the
        // end-of-tick diff can only run while tick() is being called.
        void endAllServices(IBTTaskExecutor* executor);

        Blackboard& getBlackboard() { return blackboard; }
        const Blackboard& getBlackboard() const { return blackboard; }

        const BehaviorTreeData& getTreeData() const { return *treeData; }
        bool hasTreeData() const { return treeData != nullptr; }
        services::EntityHandle getOwnerEntity() const { return ownerEntity; }
        const std::unordered_map<uint32_t, BTNodeRuntime>& getNodeStates() const { return nodeStates; }

    private:
        BTNodeStatus tickNode(uint32_t nodeId, float dt, IBTTaskExecutor* executor);
        BTNodeStatus tickComposite(const BTNode& node, float dt, IBTTaskExecutor* executor);
        BTNodeStatus tickDecorator(const BTNode& node, float dt, IBTTaskExecutor* executor);
        BTNodeStatus tickService(const BTNode& node, float dt, IBTTaskExecutor* executor);
        BTNodeStatus tickTask(const BTNode& node, float dt, IBTTaskExecutor* executor);

        BTNodeRuntime& getNodeState(uint32_t nodeId);
        void resetSubtreeState(uint32_t nodeId);
        void abortSubtree(uint32_t nodeId, IBTTaskExecutor* executor);
        bool evaluateCondition(const BTNode& node) const;

        // Version-gated re-evaluation of a BlackboardCondition (identical result to evaluateCondition,
        // but skips the compare while the watched key's blackboard version is unchanged).
        bool observeCondition(const BTNode& node);

        // End-of-tick reconciliation: any service not visited this frame left the active path -> end it.
        void endInactiveServices(IBTTaskExecutor* executor);

        // Deterministic per-fire interval: base +/- randomDeviation, reproducible from (entity,node,fire).
        float computeServiceInterval(const BTNode& node, uint32_t fireCount) const;
    };
}

#pragma once

#include "BehaviorTreeTypes.hpp"
#include "Blackboard.hpp"
#include <unordered_map>
#include <memory>

namespace behaviortree
{
    // Per-node runtime state
    struct BTNodeRuntime
    {
        BTNodeStatus lastStatus = BTNodeStatus::Failure;
        int currentChildIndex = 0;
        float elapsedTime = 0.0f;
        int repeatCount = 0;
        bool isFirstTick = true;
    };

    // Callback interface for task execution (implemented by adapter)
    class IBTTaskExecutor
    {
    public:
        virtual ~IBTTaskExecutor() = default;

        virtual BTNodeStatus executeMoveTo(services::EntityHandle entity,
                                           const std::string& targetKey,
                                           float arrivalDistance,
                                           Blackboard& blackboard) = 0;

        virtual BTNodeStatus executePlayAnimation(services::EntityHandle entity,
                                                   const std::string& stateName,
                                                   bool waitForCompletion) = 0;

        virtual BTNodeStatus executeScriptTask(services::EntityHandle entity,
                                                const std::string& scriptPath,
                                                const std::string& className,
                                                Blackboard& blackboard,
                                                float deltaTime) = 0;

        virtual BTNodeStatus executeLog(const std::string& message, LogLevel level) = 0;
    };

    class BehaviorTreeRuntime
    {
    public:
        void init(const BehaviorTreeData& data, services::EntityHandle entity);
        BTNodeStatus tick(float deltaTime, IBTTaskExecutor* executor);
        void reset();

        Blackboard& getBlackboard() { return blackboard; }
        const Blackboard& getBlackboard() const { return blackboard; }

        const BehaviorTreeData* getTreeData() const { return treeData; }
        services::EntityHandle getOwnerEntity() const { return ownerEntity; }

    private:
        BTNodeStatus tickNode(uint32_t nodeId, float dt, IBTTaskExecutor* executor);
        BTNodeStatus tickComposite(const BTNode& node, float dt, IBTTaskExecutor* executor);
        BTNodeStatus tickDecorator(const BTNode& node, float dt, IBTTaskExecutor* executor);
        BTNodeStatus tickTask(const BTNode& node, float dt, IBTTaskExecutor* executor);

        BTNodeRuntime& getNodeState(uint32_t nodeId);

        const BehaviorTreeData* treeData = nullptr;
        services::EntityHandle ownerEntity;
        Blackboard blackboard;
        std::unordered_map<uint32_t, BTNodeRuntime> nodeStates;
    };
}

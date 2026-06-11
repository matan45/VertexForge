#pragma once
#include "../../../services/providers/ai/IBehaviorTreeProvider.hpp"
#include "../../../utilities/behaviortree/BehaviorTreeRuntime.hpp"
#include "../../../utilities/behaviortree/BehaviorTreeAsset.hpp"
#include "../../../utilities/eqs/EQSTypes.hpp"
#include <unordered_map>
#include <memory>
#include <optional>
#include <mutex>
#include <vector>
#include <atomic>

namespace services
{
    class IScriptingProvider;
}

namespace core
{
    class BehaviorTreeAdapter : public services::IBehaviorTreeProvider,
                                 public behaviortree::IBTTaskExecutor
    {
    public:
        explicit BehaviorTreeAdapter(services::IScriptingProvider* scriptingProvider = nullptr);
        ~BehaviorTreeAdapter() override;

        // === IBehaviorTreeProvider ===
        bool attachTree(services::EntityHandle entity, const std::string& treePath) override;
        void detachTree(services::EntityHandle entity) override;
        void setEnabled(services::EntityHandle entity, bool enabled) override;
        bool hasTree(services::EntityHandle entity) const override;
        std::string getTreePath(services::EntityHandle entity) const override;
        bool isEnabled(services::EntityHandle entity) const override;
        std::string getStatus(services::EntityHandle entity) const override;

        void updateAll(float deltaTime) override;
        void stopAll() override;
        void reloadAsset(const std::string& treePath) override;

        void setBlackboardValue(services::EntityHandle entity, const std::string& key,
                                const behaviortree::BlackboardValue& value) override;
        behaviortree::BlackboardValue getBlackboardValue(services::EntityHandle entity,
                                                          const std::string& key) override;
        bool hasBlackboardKey(services::EntityHandle entity, const std::string& key) const override;

        void setDebugTarget(services::EntityHandle entity) override;
        behaviortree::BTRuntimeSnapshot getRuntimeSnapshot(services::EntityHandle entity) const override;

        // === IBTTaskExecutor ===
        behaviortree::BTNodeStatus executeMoveTo(services::EntityHandle entity,
                                                  const std::string& targetKey,
                                                  float arrivalDistance,
                                                  behaviortree::Blackboard& blackboard,
                                                  bool isFirstTick) override;

        behaviortree::BTNodeStatus executePlayAnimation(services::EntityHandle entity,
                                                         const std::string& stateName,
                                                         bool waitForCompletion) override;

        behaviortree::BTNodeStatus executeScriptTask(services::EntityHandle entity,
                                                      const std::string& scriptPath,
                                                      const std::string& className,
                                                      behaviortree::Blackboard& blackboard,
                                                      float deltaTime) override;

        behaviortree::BTNodeStatus executeLog(const std::string& message,
                                               behaviortree::LogLevel level) override;

        behaviortree::BTNodeStatus executeEnvironmentQuery(
            services::EntityHandle entity,
            const std::string& queryName,
            const std::string& resultKey,
            behaviortree::Blackboard& blackboard,
            bool isFirstTick) override;

        behaviortree::BTNodeStatus executeLineOfSight(services::EntityHandle entity,
                                                       const std::string& targetKey,
                                                       float maxDistance,
                                                       float eyeOffset,
                                                       behaviortree::Blackboard& blackboard) override;

        void onAbort(services::EntityHandle entity, const behaviortree::BTNode& node) override;

    private:
        struct RuntimeInstance
        {
            std::unique_ptr<behaviortree::BehaviorTreeRuntime> runtime;
            std::string treePath;
            bool enabled = true;
            std::optional<behaviortree::BTNodeStatus> lastTickStatus;
        };

        // Collision-safe key for script instances: (entityId, scriptPath)
        struct ScriptInstanceKey
        {
            uint64_t entityId;
            std::string scriptPath;
            bool operator==(const ScriptInstanceKey& other) const
            {
                return entityId == other.entityId && scriptPath == other.scriptPath;
            }
        };

        struct ScriptInstanceKeyHash
        {
            size_t operator()(const ScriptInstanceKey& k) const
            {
                size_t h = std::hash<uint64_t>{}(k.entityId);
                h ^= std::hash<std::string>{}(k.scriptPath) + 0x9e3779b9 + (h << 6) + (h >> 2);
                return h;
            }
        };

        services::IScriptingProvider* scriptingProvider;
        std::unordered_map<uint64_t, RuntimeInstance> runtimes; // keyed by EntityHandle::id
        std::unordered_map<ScriptInstanceKey, uint64_t, ScriptInstanceKeyHash> scriptInstances;
        std::unordered_map<std::string, eqs::EQSQueryHandle> pendingEQSQueries; // key: "{entityId}:{queryName}"

        // One immutable tree per asset path, shared by every runtime attached to it
        std::unordered_map<std::string, std::shared_ptr<const behaviortree::BehaviorTreeData>> assetCache;

        // Hot-reload requests queued from the editor thread, applied between ticks in updateAll
        std::vector<std::string> pendingReloads;
        std::mutex reloadMutex;

        // Debugger: snapshot of one entity's runtime, written after its tick (worker task)
        // and read by the editor (main thread) — always copied under snapshotMutex
        std::atomic<uint64_t> debugTargetEntityId{0};
        mutable std::mutex snapshotMutex;
        behaviortree::BTRuntimeSnapshot debugSnapshot;
        uint64_t tickCounter = 0;

        std::shared_ptr<const behaviortree::BehaviorTreeData> getOrLoadTree(const std::string& treePath);
        void applyPendingReloads();
        void captureDebugSnapshot(const behaviortree::BehaviorTreeRuntime& runtime);
        void cleanupScriptInstances(uint64_t entityId, const behaviortree::BehaviorTreeData& treeData);
        void cancelPendingEQSQueriesForEntity(uint64_t entityId);
    };
}

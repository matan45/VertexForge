#pragma once
#include "../../../services/providers/ai/IBehaviorTreeProvider.hpp"
#include "../../../services/data/VolumetricNavData.hpp"
#include "../../../utilities/behaviortree/BehaviorTreeRuntime.hpp"
#include "../../../utilities/behaviortree/BehaviorTreeAsset.hpp"
#include "../../../utilities/behaviortree/MoveToTracker.hpp"
#include "../../../utilities/behaviortree/RouteFollower.hpp"
#include "../../../utilities/eqs/EQSTypes.hpp"
#include <glm/glm.hpp>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <optional>
#include <mutex>
#include <string>
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
        std::vector<services::EntityHandle> getAttachedEntities() const override;
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

        void setDynamicSubtree(services::EntityHandle entity, const std::string& tag,
                               const std::string& treePath) override;

        void setDebugTarget(services::EntityHandle entity) override;
        behaviortree::BTRuntimeSnapshot getRuntimeSnapshot(services::EntityHandle entity) const override;

        void setDebugPaused(bool paused) override;
        void stepDebug() override;
        void setBreakpoints(services::EntityHandle entity, const std::vector<uint32_t>& nodeIds) override;

        behaviortree::BTRuntimeState getRuntimeState(services::EntityHandle entity) const override;

        // === IBTTaskExecutor ===
        behaviortree::BTNodeStatus executeMoveTo(services::EntityHandle entity,
                                                  const behaviortree::MoveToRequest& request,
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
            const behaviortree::BTNode& node,
            const behaviortree::EnvironmentQueryRequest& request,
            behaviortree::Blackboard& blackboard,
            bool isFirstTick) override;

        behaviortree::BTNodeStatus executeLineOfSight(services::EntityHandle entity,
                                                       const std::string& targetKey,
                                                       float maxDistance,
                                                       float eyeOffset,
                                                       behaviortree::Blackboard& blackboard) override;

        behaviortree::BTNodeStatus executeFollowRoute(services::EntityHandle entity,
                                                       const behaviortree::BTNode& node,
                                                       const behaviortree::FollowRouteRequest& request,
                                                       behaviortree::Blackboard& blackboard,
                                                       float deltaTime,
                                                       bool isFirstTick) override;

        void onAbort(services::EntityHandle entity, const behaviortree::BTNode& node) override;

        // Service hooks (VK-1456): dispatch on the node's "serviceType" property. onServiceStart uses
        // the base no-op — the built-ins need no start-time setup.
        void onServiceTick(services::EntityHandle entity, const behaviortree::BTNode& node,
                           behaviortree::Blackboard& blackboard, float deltaTime) override;
        void onServiceEnd(services::EntityHandle entity, const behaviortree::BTNode& node,
                          behaviortree::Blackboard& blackboard) override;

    private:
        struct RuntimeInstance
        {
            std::unique_ptr<behaviortree::BehaviorTreeRuntime> runtime;
            std::string treePath; // BehaviorTreeAsset::canonicalPath form
            bool enabled = true;
            std::optional<behaviortree::BTNodeStatus> lastTickStatus;
            uint64_t tickCount = 0; // ticks since attach or the last stopAll (getRuntimeState)
        };

        // Per-(entity, node) task state. Keyed by the node's ADDRESS like the service EQS keys: a parent tree
        // and a nested dynamic subtree have independent id spaces, and cached tree data is immutable.
        struct NodeRunKey
        {
            uint64_t entityId = 0;
            const behaviortree::BTNode* node = nullptr;
            bool operator==(const NodeRunKey& other) const
            {
                return entityId == other.entityId && node == other.node;
            }
        };

        struct NodeRunKeyHash
        {
            size_t operator()(const NodeRunKey& k) const
            {
                size_t h = std::hash<uint64_t>{}(k.entityId);
                h ^= std::hash<const behaviortree::BTNode*>{}(k.node) + 0x9e3779b9 + (h << 6) + (h >> 2);
                return h;
            }
        };

        // FollowRoute progress (VK-1660). Survives an abort so re-entry resumes the leg.
        struct RouteRun
        {
            behaviortree::RouteFollowerState follower;
            std::vector<glm::vec3> points; // marker world positions snapped to the navmesh, gathered on entry
        };

        // How a blackboard target key resolved (VK-1660): Entity, Int (entity id, -1 = none) or Vec3 keys.
        enum class TargetLookup : uint8_t
        {
            Found,
            Missing,  // no such key
            Unset,    // an unset / dead entity, or an Int key holding -1
            WrongType // the key holds another type
        };

        struct BlackboardTarget
        {
            TargetLookup lookup = TargetLookup::Missing;
            glm::vec3 position{0.0f};                                          // world position when Found
            services::EntityHandle entity = services::EntityHandle::invalid(); // set for Entity / Int keys
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
        // In-flight EQS queries, keyed "{entityId}:task:{node address}" (EnvironmentQuery) or
        // "{entityId}:svc:{node address}" (EQSRefresh); released as soon as their result is read.
        std::unordered_map<std::string, eqs::EQSQueryHandle> pendingEQSQueries;

        // VK-1660 task state, all touched only from the tick (and detach / stopAll / reload between ticks).
        std::unordered_map<uint64_t, behaviortree::MoveToTrackerState> moveTrackers; // keyed by EntityHandle::id
        std::unordered_map<NodeRunKey, RouteRun, NodeRunKeyHash> routeRuns;
        // Scripts that failed to load: one warning, then the task fails without reloading every tick.
        std::unordered_set<ScriptInstanceKey, ScriptInstanceKeyHash> failedScriptLoads;
        // Warnings already logged once, keyed "{entityId}:{what}" so an entity's entries can be swept.
        std::unordered_set<std::string> loggedWarnings;

        // One immutable tree per asset path, shared by every runtime attached to it.
        // assetDependencies maps each cached root to the normalized paths of every
        // tree spliced into its expansion (so saving a subtree rebinds its parents).
        std::unordered_map<std::string, std::shared_ptr<const behaviortree::BehaviorTreeData>> assetCache;
        std::unordered_map<std::string, std::vector<std::string>> assetDependencies;
        // VK-1457 debugger: per-tree map of authored static-SubTree node id -> expanded entry node id,
        // captured during expansion. Copied into the debug target's snapshot so the editor can map
        // breakpoints/live status onto SubTree nodes (removed from the expanded id space).
        std::unordered_map<std::string, std::unordered_map<uint32_t, uint32_t>> subtreeEntryMaps;

        // Hot-reload requests queued from the editor thread, applied between ticks in updateAll
        std::vector<std::string> pendingReloads;
        std::mutex reloadMutex;

        // Debugger: snapshot of one entity's runtime, written after its tick (worker task)
        // and read by the editor (main thread) — always copied under snapshotMutex
        std::atomic<uint64_t> debugTargetEntityId{0};
        mutable std::mutex snapshotMutex;
        behaviortree::BTRuntimeSnapshot debugSnapshot;
        uint64_t tickCounter = 0;

        // VK-1457 debug controls (editor-session only). Main thread writes, worker tick reads.
        std::atomic<bool> debugPaused{false};
        std::atomic<bool> stepRequested{false};
        mutable std::mutex breakpointMutex;
        std::unordered_set<uint32_t> breakpoints; // node ids on the debug target that auto-pause when Running

        std::shared_ptr<const behaviortree::BehaviorTreeData> getOrLoadTree(const std::string& treePath);
        void applyPendingReloads();
        // valid, tickIndex, subtreeEntryMap and the per-node fields (captureSnapshotCore) - shared by the
        // debugger snapshot and getRuntimeState so both report the same live state.
        void fillSnapshotCore(const behaviortree::BehaviorTreeRuntime& runtime, const std::string& treePath,
                              behaviortree::BTRuntimeSnapshot& snapshot) const;
        void captureDebugSnapshot(const behaviortree::BehaviorTreeRuntime& runtime, const std::string& treePath);
        // Tick the debug target and apply post-tick breakpoint rising-edge detection (may set debugPaused).
        void tickDebugTarget(RuntimeInstance& instance, float deltaTime);
        void cleanupScriptInstances(uint64_t entityId);
        void cancelPendingEQSQueriesForEntity(uint64_t entityId);
        // Drops the entity's MoveTo / FollowRoute progress.
        void clearMovementState(uint64_t entityId);
        // clearMovementState plus the script-load failures and logged warnings (detach / reload).
        void clearEntityTaskState(uint64_t entityId);
        // True the first time (entityId, what) is seen since the entity's state was last cleared.
        bool warnOnce(uint64_t entityId, const std::string& what);
        void forgetWarning(uint64_t entityId, const std::string& what);

        // Shared sensing delegation, reused by both the task methods and the built-in services so the
        // task behavior stays identical. The EQS calls never throw: a failure logs once per warnKey.
        // The query's context (VK-1660): the querier's world position, ground forward and NavmeshAgent size,
        // the target from targetKey ("" = none; Entity / Vec3 / Int entity-id keys) and a per-run seed.
        eqs::EQSContext buildEQSContext(services::EntityHandle entity, const behaviortree::BTNode& node,
                                        const std::string& targetKey, const behaviortree::Blackboard& blackboard);
        eqs::EQSQueryHandle submitEQS(services::EntityHandle entity, const std::string& queryName,
                                      const eqs::EQSContext& context, const std::string& warnKey);
        // A Completed result -> resultKey / resultEntityKey (behaviortree::applyEQSResult), warning once per
        // (entity, key) about an entity key that is missing or of the wrong type.
        void writeEQSResult(services::EntityHandle entity, behaviortree::Blackboard& blackboard,
                            const eqs::EQSResult& result, const std::string& resultKey,
                            const std::string& resultEntityKey);
        eqs::EQSResult pollEQS(services::EntityHandle entity, const eqs::EQSQueryHandle& handle,
                               const std::string& warnKey);
        void cancelEQS(services::EntityHandle entity, const eqs::EQSQueryHandle& handle);

        // World position composed from the local transforms (scene::composeWorldPosition), so it reflects
        // this frame's moves; nullopt for an invalid or dead entity.
        std::optional<glm::vec3> worldPositionOf(services::EntityHandle entity) const;
        BlackboardTarget lookupTarget(const std::string& targetKey, const behaviortree::Blackboard& blackboard) const;
        // The point a sensing ray aims at: an entity eyeOffset above its pivot, a Vec3 as given. Warns when
        // the key holds another type.
        std::optional<glm::vec3> resolveTargetPosition(const std::string& targetKey, const BlackboardTarget& target,
                                                       float eyeOffset) const;
        bool computeLineOfSight(services::EntityHandle entity, const std::string& targetKey,
                                float maxDistance, float eyeOffset,
                                behaviortree::Blackboard& blackboard) const;

        // FollowRoute: the route entity's children (in child order) as world positions, snapped to the navmesh -
        // or, for a flying agent, to the voxel volumes. nullopt with `problem` set when the route cannot be
        // followed (no route, no markers, an entity with neither a NavmeshAgent nor a VolumetricAgent).
        std::optional<std::vector<glm::vec3>> gatherRoutePoints(services::EntityHandle entity,
                                                                const std::string& routeKey,
                                                                const behaviortree::Blackboard& blackboard,
                                                                std::string& problem) const;
        glm::vec3 snapToNavmesh(const glm::vec3& point) const;
        // Stops whichever agent moves the entity (navmesh, or the flying agent).
        void stopAgent(services::EntityHandle entity);

        // Flying agents (VK-1660): an entity with a VolumetricAgent and no NavmeshAgent moves through the voxel
        // volumes instead of the navmesh. Its commands never throw into the tick (a throw inside a frame task
        // terminates the process): a failure logs once per entity.
        bool isVolumetricAgent(services::EntityHandle entity) const;
        void setVolumetricDestination(services::EntityHandle entity, const glm::vec3& target);
        // nullopt when the state cannot be read (no VolumetricAgent, or no handler).
        std::optional<services::VolumetricAgentState> volumetricAgentState(services::EntityHandle entity);
        // The point when its voxel is navigable, else the nearest navigable voxel within 2 m, else the point
        // itself.
        glm::vec3 snapToVolume(const glm::vec3& point) const;
    };
}

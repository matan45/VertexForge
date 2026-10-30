#include "BehaviorTreeAdapter.hpp"
#include "../../../services/providers/scripting/IScriptingProvider.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/navmesh/NavmeshEvents.hpp"
#include "../../../services/events/physics/ControllerEvents.hpp"
#include "../../../services/events/animation/AnimatorEvents.hpp"
#include "../../../services/events/ai/EQSEvents.hpp"
#include "../../../services/events/physics/PhysicsEvents.hpp"
#include "../../../services/events/volumetric/VolumetricNavEvents.hpp"
#include "../../../services/data/EntityConversion.hpp"
#include "print/Log.hpp"
#include "behaviortree/BehaviorTreeValidation.hpp"
#include "behaviortree/BehaviorTreeDynamic.hpp"
#include "behaviortree/EQSResultWrite.hpp"
#include "behaviortree/SightLine.hpp"
#include "components/CoreComponents.hpp"
#include "components/NavmeshComponents.hpp"
#include "components/VolumetricComponents.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/HierarchyMatrix.hpp"
#include <algorithm>
#include <any>
#include <cmath>
#include <cstdint>
#include <exception>
#include <unordered_set>

namespace core
{

    using namespace behaviortree;

    namespace
    {
        // TargetDistance's value when there is no target (VK-1660): "infinitely far", so a range condition
        // such as "targetDistance <= 10" is false.
        constexpr float kNoTargetDistance = 1.0e9f;

        // A hit within this distance of the target point no longer blocks the line of sight.
        constexpr float kSightTolerance = 0.1f;

        // FollowRoute snaps markers like NavmeshAgentManager snaps destinations (SNAP_ON_MESH_TOLERANCE /
        // SNAP_SEARCH_RADII), so arrival is measured at the point the agent actually walks to.
        constexpr float kRouteSnapTolerance = 1.0f;
        constexpr float kRouteSnapRadii[] = {5.0f, 25.0f, 100.0f};

        // A flying agent's route marker inside geometry moves to the nearest navigable voxel within this (m).
        constexpr float kRouteVolumeSnapRadius = 2.0f;

        // True when `entity` is `ancestor` or below it in the hierarchy (ParentComponent chain).
        bool isSelfOrDescendant(const entt::registry& registry, services::EntityHandle entity,
                                services::EntityHandle ancestor)
        {
            if (!entity.isValid() || !ancestor.isValid())
                return false;

            constexpr int kMaxDepth = 256;
            const entt::entity target = services::internal::fromHandle(ancestor);
            entt::entity current = services::internal::fromHandle(entity);
            for (int depth = 0; depth < kMaxDepth && current != entt::null && registry.valid(current); ++depth)
            {
                if (current == target)
                    return true;
                const auto* parent = registry.try_get<components::ParentComponent>(current);
                current = parent ? parent->parent : entt::null;
            }
            return false;
        }
    }

    BehaviorTreeAdapter::BehaviorTreeAdapter(services::IScriptingProvider* scriptingProvider)
        : scriptingProvider(scriptingProvider)
    {
    }

    BehaviorTreeAdapter::~BehaviorTreeAdapter() = default;

    // getServiceProp was folded into the shared behaviortree::getNodeProperty<T> (BehaviorTreeTypes.hpp).

    // Service EQS queries key off the node's ADDRESS, not its id (VK-1457): a parent tree and a nested
    // dynamic subtree have independent id spaces (both start at 1), so keying by id would let two live
    // Service nodes on one entity clobber each other's pending handle. The cached BehaviorTreeData is
    // immutable for the session, so node addresses are stable; the "{entityId}:" prefix is preserved so
    // cancelPendingEQSQueriesForEntity() still sweeps them.
    static std::string eqsServiceKey(uint64_t entityId, const BTNode& node)
    {
        return std::to_string(entityId) + ":svc:" + std::to_string(reinterpret_cast<uintptr_t>(&node));
    }

    // EnvironmentQuery tasks key the same way (VK-1660): two EnvironmentQuery nodes of one entity running the
    // same template no longer share one pending handle.
    static std::string eqsTaskKey(uint64_t entityId, const BTNode& node)
    {
        return std::to_string(entityId) + ":task:" + std::to_string(reinterpret_cast<uintptr_t>(&node));
    }

    static uint64_t splitMix64(uint64_t value)
    {
        value += 0x9E3779B97F4A7C15ull;
        value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
        value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
        return value ^ (value >> 31);
    }

    // A reproducible per-run EQS seed (NavMeshPoints sampling): every tick samples anew, and the same entity,
    // node and tick always sample the same points.
    static uint64_t eqsSeed(uint64_t entityId, uint32_t nodeId, uint64_t tick)
    {
        return splitMix64(splitMix64(splitMix64(entityId) ^ nodeId) ^ tick);
    }

    static void logValidationDiagnostics(const std::string& treePath,
                                         const behaviortree::validation::ValidationReport& report)
    {
        for (const auto& diagnostic : report.diagnostics)
        {
            if (diagnostic.severity == behaviortree::validation::Severity::Info)
                continue;

            const std::string nodePrefix = diagnostic.nodeId == 0
                                               ? std::string{}
                                               : "node " + std::to_string(diagnostic.nodeId) + ": ";
            if (diagnostic.severity == behaviortree::validation::Severity::Error)
            {
                vfLogError("BT validation error in '{}': {}{}", treePath, nodePrefix, diagnostic.message);
            }
            else
            {
                vfLogWarning("BT validation warning in '{}': {}{}", treePath, nodePrefix, diagnostic.message);
            }
        }
    }

    static bool validateExpandedOrLog(const BehaviorTreeData& data, const std::string& treePath)
    {
        behaviortree::validation::ValidationContext context;
        context.allowSubTrees = false;

        auto report = behaviortree::validation::validateBehaviorTree(data, context);
        logValidationDiagnostics(treePath, report);

        if (report.hasErrors())
        {
            vfLogError("BT validation refused '{}': {} error(s), {} warning(s)",
                       treePath, report.errorCount(), report.warningCount());
            return false;
        }
        return true;
    }

    std::shared_ptr<const BehaviorTreeData> BehaviorTreeAdapter::getOrLoadTree(const std::string& treePath)
    {
        const std::string key = BehaviorTreeAsset::canonicalPath(treePath);
        auto cacheIt = assetCache.find(key);
        if (cacheIt != assetCache.end())
            return cacheIt->second;

        std::vector<std::string> dependencies;
        std::unordered_map<uint32_t, uint32_t> subtreeEntryMap;
        auto dataOpt = BehaviorTreeAsset::loadExpanded(key, &dependencies, &subtreeEntryMap);
        if (!dataOpt.has_value())
            return nullptr;
        // Validation is advisory at attach time: log any errors/warnings but still attach. The runtime
        // degrades gracefully (a malformed branch returns Failure), so refusing to load would disable the
        // entity's entire AI over one bad branch (VK-1457 regression). Hot reload keeps its stricter gate.
        validateExpandedOrLog(dataOpt.value(), key);

        for (auto& dependency : dependencies)
            dependency = BehaviorTreeAsset::canonicalPath(dependency);

        auto shared = std::make_shared<const BehaviorTreeData>(std::move(dataOpt.value()));
        assetCache[key] = shared;
        assetDependencies[key] = std::move(dependencies);
        subtreeEntryMaps[key] = std::move(subtreeEntryMap);
        return shared;
    }

    bool BehaviorTreeAdapter::attachTree(services::EntityHandle entity, const std::string& treePath)
    {
        if (!entity.isValid() || treePath.empty())
            return false;

        const std::string key = BehaviorTreeAsset::canonicalPath(treePath);
        auto treeData = getOrLoadTree(key);
        if (!treeData)
        {
            vfLogError("Failed to load behavior tree: {}", treePath);
            return false;
        }

        detachTree(entity);

        RuntimeInstance instance;
        instance.runtime = std::make_unique<BehaviorTreeRuntime>();
        instance.runtime->init(treeData, entity);
        // VK-1457: dynamic subtrees resolve their target through the same cache as static loads, and the
        // cycle/depth guard is seeded with this root's path - canonical and as given, because the runtime
        // compares DynamicSubTree paths as written.
        instance.runtime->setTreeResolver([this](const std::string& path) { return getOrLoadTree(path); });
        std::vector<std::string> ancestors{key};
        std::string spelled = treePath;
        std::replace(spelled.begin(), spelled.end(), '\\', '/');
        if (spelled != key)
            ancestors.push_back(std::move(spelled));
        instance.runtime->setNestingContext(0, std::move(ancestors));
        instance.treePath = key;
        instance.enabled = true;

        runtimes[entity.id] = std::move(instance);
        vfLogInfo("Attached behavior tree '{}' to entity {}", treePath, entity.id);
        return true;
    }

    // Destroy + unload every script instance owned by this entity (VK-1457). Sweeping by entity id rather
    // than walking a single graph covers parent + nested dynamic-subtree + swapped-out scripts uniformly.
    void BehaviorTreeAdapter::cleanupScriptInstances(uint64_t entityId)
    {
        if (!scriptingProvider) return;

        for (auto sit = scriptInstances.begin(); sit != scriptInstances.end(); )
        {
            if (sit->first.entityId != entityId)
            {
                ++sit;
                continue;
            }
            if (scriptingProvider->isScriptLoaded(sit->second))
            {
                scriptingProvider->callOnDestroy(sit->second);
                scriptingProvider->unloadScript(sit->second);
            }
            sit = scriptInstances.erase(sit);
        }
    }

    void BehaviorTreeAdapter::cancelPendingEQSQueriesForEntity(uint64_t entityId)
    {
        std::string prefix = std::to_string(entityId) + ":";
        for (auto eqsIt = pendingEQSQueries.begin(); eqsIt != pendingEQSQueries.end(); )
        {
            if (eqsIt->first.compare(0, prefix.size(), prefix) == 0)
            {
                cancelEQS(services::EntityHandle{entityId}, eqsIt->second);
                eqsIt = pendingEQSQueries.erase(eqsIt);
            }
            else
                ++eqsIt;
        }
    }

    void BehaviorTreeAdapter::detachTree(services::EntityHandle entity)
    {
        auto it = runtimes.find(entity.id);
        if (it == runtimes.end()) return;

        // Abort in-flight work first (fires onAbort for running tasks incl. scripts + flushes services,
        // recursing into nested dynamic subtrees), THEN destroy the scripts, THEN sweep service EQS.
        if (it->second.runtime)
        {
            it->second.runtime->abortAll(this);
        }
        cleanupScriptInstances(entity.id);
        cancelPendingEQSQueriesForEntity(entity.id);
        clearEntityTaskState(entity.id);

        runtimes.erase(it);
    }

    void BehaviorTreeAdapter::setEnabled(services::EntityHandle entity, bool enabled)
    {
        auto it = runtimes.find(entity.id);
        if (it != runtimes.end())
        {
            // A disabled runtime stops being ticked, so flush its services now (the end-of-tick
            // diff can't run while it's not ticking). Movement progress is dropped too: on re-enable
            // MoveTo re-issues its target and FollowRoute starts over from the nearest marker.
            if (!enabled && it->second.runtime)
            {
                it->second.runtime->endAllServices(this);
                clearMovementState(entity.id);
            }
            it->second.enabled = enabled;
        }
    }

    bool BehaviorTreeAdapter::hasTree(services::EntityHandle entity) const
    {
        return runtimes.find(entity.id) != runtimes.end();
    }

    std::string BehaviorTreeAdapter::getTreePath(services::EntityHandle entity) const
    {
        auto it = runtimes.find(entity.id);
        return it != runtimes.end() ? it->second.treePath : "";
    }

    std::vector<services::EntityHandle> BehaviorTreeAdapter::getAttachedEntities() const
    {
        std::vector<services::EntityHandle> result;
        result.reserve(runtimes.size());
        for (const auto& [entityId, _] : runtimes)
            result.push_back(services::EntityHandle{entityId});
        return result;
    }

    bool BehaviorTreeAdapter::isEnabled(services::EntityHandle entity) const
    {
        auto it = runtimes.find(entity.id);
        return it != runtimes.end() && it->second.enabled;
    }

    std::string BehaviorTreeAdapter::getStatus(services::EntityHandle entity) const
    {
        auto it = runtimes.find(entity.id);
        if (it == runtimes.end()) return "stopped";
        if (!it->second.enabled) return "stopped";
        if (!it->second.lastTickStatus.has_value()) return "stopped";

        switch (*it->second.lastTickStatus)
        {
        case behaviortree::BTNodeStatus::Running: return "running";
        case behaviortree::BTNodeStatus::Success: return "success";
        case behaviortree::BTNodeStatus::Failure: return "failure";
        default: return "stopped";
        }
    }

    void BehaviorTreeAdapter::reloadAsset(const std::string& treePath)
    {
        if (treePath.empty()) return;

        const std::string key = BehaviorTreeAsset::canonicalPath(treePath);
        std::lock_guard<std::mutex> lock(reloadMutex);
        for (const auto& pending : pendingReloads)
        {
            if (pending == key) return;
        }
        pendingReloads.push_back(key);
    }

    void BehaviorTreeAdapter::applyPendingReloads()
    {
        std::vector<std::string> reloads;
        {
            std::lock_guard<std::mutex> lock(reloadMutex);
            reloads.swap(pendingReloads);
        }
        if (reloads.empty()) return;

        // Saving a subtree must also rebind every cached root that spliced it in
        std::vector<std::string> rootsToReload;
        auto addRoot = [&rootsToReload](const std::string& path)
        {
            if (std::find(rootsToReload.begin(), rootsToReload.end(), path) == rootsToReload.end())
                rootsToReload.push_back(path);
        };

        // Every key here is in BehaviorTreeAsset::canonicalPath form (reloadAsset, the cache and the
        // dependency lists), so a script path, a component path and an authoring save of one file match.
        for (const auto& savedPath : reloads)
        {
            addRoot(savedPath);

            for (const auto& [rootPath, dependencies] : assetDependencies)
            {
                if (rootPath == savedPath) continue;
                if (std::find(dependencies.begin(), dependencies.end(), savedPath) != dependencies.end())
                    addRoot(rootPath);
            }
        }

        for (const auto& treePath : rootsToReload)
        {
            std::vector<std::string> dependencies;
            std::unordered_map<uint32_t, uint32_t> subtreeEntryMap;
            auto dataOpt = BehaviorTreeAsset::loadExpanded(treePath, &dependencies, &subtreeEntryMap);
            if (!dataOpt.has_value())
            {
                vfLogError("BT hot reload: failed to load '{}', keeping old tree", treePath);
                continue;
            }
            if (!validateExpandedOrLog(dataOpt.value(), treePath))
            {
                vfLogError("BT hot reload: validation failed for '{}', keeping old tree", treePath);
                continue;
            }

            for (auto& dependency : dependencies)
                dependency = BehaviorTreeAsset::canonicalPath(dependency);

            auto newData = std::make_shared<const BehaviorTreeData>(std::move(dataOpt.value()));
            assetCache[treePath] = newData;
            assetDependencies[treePath] = std::move(dependencies);
            subtreeEntryMaps[treePath] = std::move(subtreeEntryMap);

            int rebound = 0;
            for (auto& [entityId, instance] : runtimes)
            {
                if (instance.treePath != treePath || !instance.runtime) continue;

                // Old node ids/scripts/nested subtrees are invalid against the new graph
                cleanupScriptInstances(entityId);
                cancelPendingEQSQueriesForEntity(entityId);
                instance.runtime->endAllServices(this);
                clearEntityTaskState(entityId);

                auto savedBlackboard = instance.runtime->getBlackboard().getAll();
                services::EntityHandle owner = instance.runtime->getOwnerEntity();

                instance.runtime->init(newData, owner);
                instance.lastTickStatus.reset();

                // Restore surviving values: keep script scratch keys, and declared keys
                // whose stored type still matches the new schema default
                auto& blackboard = instance.runtime->getBlackboard();
                for (const auto& [key, value] : savedBlackboard)
                {
                    bool declared = false;
                    bool typeMatches = true;
                    for (const auto& keyDef : newData->graph.blackboardKeys)
                    {
                        if (keyDef.name == key)
                        {
                            declared = true;
                            typeMatches = keyDef.defaultValue.index() == value.index();
                            break;
                        }
                    }
                    if (!declared || typeMatches)
                    {
                        blackboard.set(key, value);
                    }
                }
                ++rebound;
            }

            vfLogInfo("BT hot reload: '{}' rebound to {} runtime(s)", treePath, rebound);
        }
    }

    void BehaviorTreeAdapter::fillSnapshotCore(const BehaviorTreeRuntime& runtime, const std::string& treePath,
                                               BTRuntimeSnapshot& snapshot) const
    {
        snapshot.valid = true;
        snapshot.tickIndex = tickCounter;

        // VK-1457: hand the editor the authored-SubTree -> expanded-entry id map for this tree so it can
        // translate breakpoints and mirror live status onto SubTree nodes (empty for trees with none).
        auto mapIt = subtreeEntryMaps.find(treePath);
        if (mapIt != subtreeEntryMaps.end())
            snapshot.subtreeEntryMap = mapIt->second;

        // Per-node statuses / child indices / elapsed / last results / active path + sorted blackboard.
        const BTGraph& graph = runtime.getTreeData().graph;
        behaviortree::captureSnapshotCore(graph, runtime.getNodeStates(), runtime.getBlackboard(),
                                          graph.rootNodeId, snapshot);
    }

    void BehaviorTreeAdapter::captureDebugSnapshot(const BehaviorTreeRuntime& runtime, const std::string& treePath)
    {
        if (!runtime.hasTreeData()) return;

        BTRuntimeSnapshot snapshot;
        fillSnapshotCore(runtime, treePath, snapshot);

        // Bounded history + aborts + which dynamic subtree is running are recorded by the runtime itself
        // (only while debugRecording is enabled, i.e. for this target).
        snapshot.abortRecords = runtime.getAbortRecords();
        snapshot.executionEvents = runtime.getExecutionEvents();
        snapshot.activeDynamicSubtreePath = runtime.getActiveDynamicSubtreePath();
        snapshot.paused = debugPaused.load(std::memory_order_relaxed);

        std::lock_guard<std::mutex> lock(snapshotMutex);
        debugSnapshot = std::move(snapshot);
    }

    void BehaviorTreeAdapter::tickDebugTarget(RuntimeInstance& instance, float deltaTime)
    {
        // Copy the breakpoint set under the lock (the editor may replace it on the main thread).
        std::unordered_set<uint32_t> bps;
        {
            std::lock_guard<std::mutex> lock(breakpointMutex);
            bps = breakpoints;
        }

        // Which breakpointed nodes were already Running BEFORE this tick (for rising-edge detection).
        std::unordered_set<uint32_t> runningBefore;
        if (!bps.empty())
        {
            for (const auto& [nodeId, state] : instance.runtime->getNodeStates())
            {
                if (state.lastStatus == BTNodeStatus::Running && bps.count(nodeId))
                    runningBefore.insert(nodeId);
            }
        }

        instance.lastTickStatus = instance.runtime->tick(deltaTime, this);
        ++instance.tickCount;

        // Rising edge: a breakpointed node became Running that wasn't before -> freeze at end of tick.
        if (!bps.empty())
        {
            for (const auto& [nodeId, state] : instance.runtime->getNodeStates())
            {
                if (state.lastStatus == BTNodeStatus::Running && bps.count(nodeId) &&
                    runningBefore.find(nodeId) == runningBefore.end())
                {
                    debugPaused.store(true, std::memory_order_relaxed);
                    break;
                }
            }
        }
    }

    void BehaviorTreeAdapter::updateAll(float deltaTime)
    {
        applyPendingReloads();
        ++tickCounter;

        std::vector<uint64_t> entityIds;
        entityIds.reserve(runtimes.size());
        for (const auto& [entityId, _] : runtimes)
            entityIds.push_back(entityId);

        uint64_t debugId = debugTargetEntityId.load(std::memory_order_relaxed);
        const bool paused = debugPaused.load(std::memory_order_relaxed);

        for (uint64_t entityId : entityIds)
        {
            auto it = runtimes.find(entityId);
            if (it == runtimes.end()) continue;

            auto& instance = it->second;
            if (!instance.enabled || !instance.runtime) continue;

            const bool isTarget = (entityId == debugId);
            instance.runtime->setDebugRecording(isTarget); // only the target records history (worker-side)

            if (!isTarget)
            {
                instance.lastTickStatus = instance.runtime->tick(deltaTime, this);
                ++instance.tickCount;
                continue;
            }

            if (paused)
            {
                // Frozen: advance only on an explicit step. Refresh the snapshot either way so the editor
                // sees live blackboard edits and the paused flag.
                if (stepRequested.exchange(false, std::memory_order_relaxed))
                    tickDebugTarget(instance, deltaTime);
            }
            else
            {
                tickDebugTarget(instance, deltaTime); // may trip a breakpoint and set debugPaused
            }
            captureDebugSnapshot(*instance.runtime, instance.treePath);
        }
    }

    void BehaviorTreeAdapter::setDynamicSubtree(services::EntityHandle entity, const std::string& tag,
                                                const std::string& treePath)
    {
        auto it = runtimes.find(entity.id);
        if (it == runtimes.end() || !it->second.runtime) return;

        if (treePath.empty())
            it->second.runtime->clearDynamicInjection(tag);
        else
            it->second.runtime->setDynamicInjection(tag, treePath);
    }

    void BehaviorTreeAdapter::setDebugTarget(services::EntityHandle entity)
    {
        debugTargetEntityId.store(entity.isValid() ? entity.id : 0);

        // Switching (or clearing) the target starts a fresh debug session.
        debugPaused.store(false, std::memory_order_relaxed);
        stepRequested.store(false, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(breakpointMutex);
            breakpoints.clear();
        }

        std::lock_guard<std::mutex> lock(snapshotMutex);
        debugSnapshot = {};
    }

    void BehaviorTreeAdapter::setDebugPaused(bool paused)
    {
        debugPaused.store(paused, std::memory_order_relaxed);
    }

    void BehaviorTreeAdapter::stepDebug()
    {
        stepRequested.store(true, std::memory_order_relaxed);
    }

    void BehaviorTreeAdapter::setBreakpoints(services::EntityHandle entity, const std::vector<uint32_t>& nodeIds)
    {
        (void)entity; // breakpoints only apply to the current debug target
        std::lock_guard<std::mutex> lock(breakpointMutex);
        breakpoints.clear();
        breakpoints.insert(nodeIds.begin(), nodeIds.end());
    }

    BTRuntimeSnapshot BehaviorTreeAdapter::getRuntimeSnapshot(services::EntityHandle entity) const
    {
        if (!entity.isValid() || entity.id != debugTargetEntityId.load(std::memory_order_relaxed))
            return {};

        std::lock_guard<std::mutex> lock(snapshotMutex);
        return debugSnapshot;
    }

    // VK-1660: built from the live runtime when asked (between ticks: the MCP drain runs before the frame
    // graph), so it never needs the debug target and records no history.
    BTRuntimeState BehaviorTreeAdapter::getRuntimeState(services::EntityHandle entity) const
    {
        BTRuntimeState state;
        state.status = "stopped";

        auto it = runtimes.find(entity.id);
        if (!entity.isValid() || it == runtimes.end() || !it->second.runtime)
            return state;

        const RuntimeInstance& instance = it->second;
        const BehaviorTreeRuntime& runtime = *instance.runtime;
        state.attached = true;
        state.enabled = instance.enabled;
        state.treePath = instance.treePath;
        state.status = getStatus(entity);
        state.tickCount = instance.tickCount;
        if (!runtime.hasTreeData())
            return state;

        // The cache entry is the very tree the runtime ticks unless a hot reload swapped it (then copy).
        auto cacheIt = assetCache.find(instance.treePath);
        if (cacheIt != assetCache.end() && cacheIt->second.get() == &runtime.getTreeData())
            state.tree = cacheIt->second;
        else
            state.tree = std::make_shared<const BehaviorTreeData>(runtime.getTreeData());

        fillSnapshotCore(runtime, instance.treePath, state.snapshot);
        // The runtime records which dynamic subtree runs only while it is the editor's debug target.
        if (runtime.isDebugRecording())
            state.snapshot.activeDynamicSubtreePath = runtime.getActiveDynamicSubtreePath();
        state.snapshot.paused = entity.id == debugTargetEntityId.load(std::memory_order_relaxed) &&
                                debugPaused.load(std::memory_order_relaxed);
        return state;
    }

    void BehaviorTreeAdapter::stopAll()
    {
        for (auto& [entityId, instance] : runtimes)
        {
            if (instance.runtime)
            {
                // Abort in-flight work (cancels nav/EQS/scripts + flushes nested services), destroy the
                // entity's scripts, then reset the tree for a clean next play session.
                instance.runtime->abortAll(this);
                cleanupScriptInstances(entityId);
                cancelPendingEQSQueriesForEntity(entityId);
                instance.runtime->reset();
            }
            instance.tickCount = 0;
        }
        // Play session over: drop cached assets so the next session reloads from disk
        assetCache.clear();
        assetDependencies.clear();
        subtreeEntryMaps.clear();
        moveTrackers.clear();
        routeRuns.clear();
        failedScriptLoads.clear();
        loggedWarnings.clear();

        setDebugTarget(services::EntityHandle::invalid());
    }

    void BehaviorTreeAdapter::setBlackboardValue(services::EntityHandle entity, const std::string& key,
                                                  const BlackboardValue& value)
    {
        auto it = runtimes.find(entity.id);
        if (it != runtimes.end() && it->second.runtime)
            it->second.runtime->getBlackboard().set(key, value);
    }

    BlackboardValue BehaviorTreeAdapter::getBlackboardValue(services::EntityHandle entity,
                                                             const std::string& key)
    {
        auto it = runtimes.find(entity.id);
        if (it != runtimes.end() && it->second.runtime)
            return it->second.runtime->getBlackboard().get(key);
        return 0.0f;
    }

    bool BehaviorTreeAdapter::hasBlackboardKey(services::EntityHandle entity, const std::string& key) const
    {
        auto it = runtimes.find(entity.id);
        if (it != runtimes.end() && it->second.runtime)
            return it->second.runtime->getBlackboard().has(key);
        return false;
    }

    BTNodeStatus BehaviorTreeAdapter::executeMoveTo(services::EntityHandle entity,
                                                     const MoveToRequest& request,
                                                     Blackboard& blackboard,
                                                     bool isFirstTick)
    {
        if (!blackboard.has(request.targetKey))
        {
            vfLogWarning("BT MoveTo: blackboard key '{}' not found", request.targetKey);
            moveTrackers.erase(entity.id);
            return BTNodeStatus::Failure;
        }

        // VK-1660: the target is a point (Vec3) or an entity it follows (Entity, or Int holding an entity id).
        const BlackboardTarget target = lookupTarget(request.targetKey, blackboard);
        if (target.lookup != TargetLookup::Found)
        {
            if (target.lookup == TargetLookup::WrongType && warnOnce(entity.id, "moveto:" + request.targetKey))
            {
                vfLogWarning("BT MoveTo: blackboard key '{}' is not a Vec3, Entity or Int (entity id) key",
                             request.targetKey);
            }
            // An unset or dead target entity: nothing to walk to.
            moveTrackers.erase(entity.id);
            return BTNodeStatus::Failure;
        }

        auto& dispatcher = events::EventDispatcher::instance();
        if (isFirstTick)
        {
            events::controller::SetArrivalDistanceCommand arrivalCmd;
            arrivalCmd.entity = entity;
            arrivalCmd.arrivalDistance = request.arrivalDistance;
            dispatcher.execute(arrivalCmd);
        }

        // Arrival is a direct horizontal distance test (stepMoveTo: against the target's CURRENT position, or
        // the first destination when repathDistance is 0). MoveTo issues the destination through the navmesh
        // agent, but the controller-based HasReachedDestinationQuery only works for entities that have a
        // ControllerComponent and returns true for everything else (e.g. a NavmeshAgent-driven unit), which
        // collapses the loop. A distance test works for any movement system (mirrors the legacy harvester's
        // arrived()). An owner without a position counts as arrived.
        //
        // The first tick only issues the destination (stepMoveTo defers arrival by one tick). The navmesh
        // agent updates AFTER the behavior tree in the frame graph (Controllers -> BehaviorTrees -> Navmesh),
        // so the agent still holds the PREVIOUS destination this frame; reporting "arrived" the instant a new
        // move is issued (e.g. the return-home leg right after reaching the resource) collapses the loop and
        // pins the agent in place.
        //
        // A flying agent (VolumetricAgent, no NavmeshAgent) moves through the voxel volumes and arrives in 3D.
        const bool flying = isVolumetricAgent(entity);
        const glm::vec3 ownerPosition = worldPositionOf(entity).value_or(target.position);
        MoveToTrackerState& tracker = moveTrackers[entity.id];
        const MoveToStep step = stepMoveTo(tracker, request, ownerPosition, target.position, isFirstTick, !flying);

        switch (step.action)
        {
        case MoveToAction::IssueDestination:
        {
            if (flying)
            {
                setVolumetricDestination(entity, target.position);
            }
            else
            {
                events::navmesh::SetAgentDestinationCommand navCmd;
                navCmd.entity = entity;
                navCmd.target = target.position;
                dispatcher.execute(navCmd);
            }
            break;
        }
        case MoveToAction::Stop:
            stopAgent(entity);
            break;
        case MoveToAction::None:
            break;
        }

        // A flying agent reports its own outcome: Failed (no path, stuck) fails the task, and Arrived counts as
        // arrival - its stoppingDistance can hold it outside arrivalDistance - while the target is still near the
        // point it was sent to. Not read on a tick that (re)issued the destination: from the next tick on the
        // state is the new move's.
        if (flying && step.status == BTNodeStatus::Running && step.action != MoveToAction::IssueDestination)
        {
            if (const auto state = volumetricAgentState(entity))
            {
                if (state->state == services::VolumetricAgentMoveState::Failed)
                {
                    if (warnOnce(entity.id, "moveto:flight"))
                    {
                        vfLogWarning("BT MoveTo: flying agent {} cannot reach its target: {}", entity.id,
                                     state->failReason);
                    }
                    moveTrackers.erase(entity.id);
                    return BTNodeStatus::Failure;
                }

                const float withinReach = std::max(request.repathDistance, request.arrivalDistance);
                if (state->state == services::VolumetricAgentMoveState::Arrived &&
                    glm::distance(target.position, tracker.issuedTarget) <= withinReach)
                {
                    if (request.stopOnArrival)
                        stopAgent(entity);
                    forgetWarning(entity.id, "moveto:flight");
                    moveTrackers.erase(entity.id);
                    return BTNodeStatus::Success;
                }
            }
        }

        if (step.status != BTNodeStatus::Running)
            moveTrackers.erase(entity.id);
        return step.status;
    }

    BTNodeStatus BehaviorTreeAdapter::executePlayAnimation(services::EntityHandle entity,
                                                            const std::string& stateName,
                                                            bool waitForCompletion)
    {
        if (stateName.empty())
            return BTNodeStatus::Failure;

        auto& dispatcher = events::EventDispatcher::instance();

        services::events::animator::ForceEntityTransitionToCommand transitionCmd;
        transitionCmd.entity = entity;
        transitionCmd.stateName = stateName;
        dispatcher.execute(transitionCmd);

        if (waitForCompletion)
        {
            services::events::animator::GetEntityAnimatorNormalizedTimeQuery timeQuery;
            timeQuery.entity = entity;
            float normalizedTime = dispatcher.query(timeQuery);
            return normalizedTime >= 1.0f ? BTNodeStatus::Success : BTNodeStatus::Running;
        }

        return BTNodeStatus::Success;
    }

    BTNodeStatus BehaviorTreeAdapter::executeScriptTask(services::EntityHandle entity,
                                                         const std::string& scriptPath,
                                                         const std::string& className,
                                                         Blackboard& blackboard,
                                                         float deltaTime)
    {
        if (!scriptingProvider || scriptPath.empty() || className.empty())
            return BTNodeStatus::Failure;

        ScriptInstanceKey scriptKey{entity.id, scriptPath};
        auto it = scriptInstances.find(scriptKey);
        if (it == scriptInstances.end())
        {
            // VK-1660: a script that failed to load is not retried every tick (one warning, then Failure)
            // until the tree is detached or reloaded, or Play stops.
            if (failedScriptLoads.contains(scriptKey))
                return BTNodeStatus::Failure;

            auto info = scriptingProvider->loadScript(scriptPath, entity);
            if (!info.has_value())
            {
                vfLogWarning("BT ScriptTask: failed to load script '{}' for entity {} (not retried until the "
                             "tree reloads)", scriptPath, entity.id);
                failedScriptLoads.insert(scriptKey);
                return BTNodeStatus::Failure;
            }
            scriptInstances[scriptKey] = info->instanceId;
            scriptingProvider->callOnStart(info->instanceId);
            it = scriptInstances.find(scriptKey);
        }

        std::string result = scriptingProvider->callMethodWithReturn(
            it->second, "tick", {std::any(deltaTime)});

        // VK-1457: fire the optional onEnd(success) completion hook when the task returns a terminal
        // status. onAbort covers the preempted path; onEnd covers natural success/failure completion.
        const bool running = (result == "running");
        if (!running && scriptingProvider->hasMethod(it->second, "onEnd"))
        {
            scriptingProvider->callMethodWithReturn(it->second, "onEnd", {std::any(result == "success")});
        }

        if (result == "success") return BTNodeStatus::Success;
        if (running) return BTNodeStatus::Running;
        return BTNodeStatus::Failure;
    }

    BTNodeStatus BehaviorTreeAdapter::executeLog(const std::string& message, LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Info:    vfLogInfo("[BT] {}", message); break;
        case LogLevel::Warn:    vfLogWarning("[BT] {}", message); break;
        case LogLevel::Error:   vfLogError("[BT] {}", message); break;
        }
        return BTNodeStatus::Success;
    }

    BTNodeStatus BehaviorTreeAdapter::executeEnvironmentQuery(
        services::EntityHandle entity,
        const BTNode& node,
        const EnvironmentQueryRequest& request,
        Blackboard& blackboard,
        bool isFirstTick)
    {
        if (request.queryName.empty())
        {
            if (warnOnce(entity.id, "eqs:no-query-name"))
                vfLogWarning("BT EnvironmentQuery '{}': no query name for entity {}", node.name, entity.id);
            return BTNodeStatus::Failure;
        }

        const std::string warnKey = "eqs:" + request.queryName;
        const std::string eqsKey = eqsTaskKey(entity.id, node);
        if (isFirstTick)
        {
            // A query of an earlier run of this node that was never read is dropped, not leaked.
            auto stale = pendingEQSQueries.find(eqsKey);
            if (stale != pendingEQSQueries.end())
            {
                cancelEQS(entity, stale->second);
                pendingEQSQueries.erase(stale);
            }

            const eqs::EQSContext context = buildEQSContext(entity, node, request.targetKey, blackboard);
            auto handle = submitEQS(entity, request.queryName, context, warnKey);
            if (!handle.isValid())
            {
                if (warnOnce(entity.id, warnKey + ":submit"))
                {
                    vfLogWarning("BT EnvironmentQuery: failed to submit query '{}' for entity {} (no valid "
                                 "assets/eqs/{}.vfEQSQuery, or the queue is full)",
                                 request.queryName, entity.id, request.queryName);
                }
                return BTNodeStatus::Failure;
            }

            pendingEQSQueries[eqsKey] = handle;
            return BTNodeStatus::Running;
        }

        // Poll for results on subsequent ticks
        auto it = pendingEQSQueries.find(eqsKey);
        if (it == pendingEQSQueries.end())
        {
            return BTNodeStatus::Failure;
        }

        const eqs::EQSResult result = pollEQS(entity, it->second, warnKey);
        if (result.status == eqs::EQSQueryStatus::Pending || result.status == eqs::EQSQueryStatus::Running)
        {
            return BTNodeStatus::Running;
        }

        // Finished: free the result at once (VK-1660 - results used to stay in the engine forever).
        cancelEQS(entity, it->second);
        pendingEQSQueries.erase(it);

        if (result.status == eqs::EQSQueryStatus::Failed)
        {
            if (!result.error.empty() && warnOnce(entity.id, warnKey + ":failed"))
            {
                vfLogWarning("BT EnvironmentQuery '{}': query '{}' failed for entity {}: {}",
                             node.name, request.queryName, entity.id, result.error);
            }
            return BTNodeStatus::Failure;
        }

        writeEQSResult(entity, blackboard, result, request.resultKey, request.resultEntityKey);
        return result.hasResults() ? BTNodeStatus::Success : BTNodeStatus::Failure;
    }

    void BehaviorTreeAdapter::onAbort(services::EntityHandle entity, const BTNode& node)
    {
        switch (node.type)
        {
        case BTNodeType::MoveTo:
        {
            stopAgent(entity);
            moveTrackers.erase(entity.id);
            break;
        }
        case BTNodeType::FollowRoute:
        {
            // Keep the route progress: re-entry resumes the current leg (RouteFollower re-entry rules).
            stopAgent(entity);
            break;
        }
        case BTNodeType::EnvironmentQuery:
        {
            auto it = pendingEQSQueries.find(eqsTaskKey(entity.id, node));
            if (it != pendingEQSQueries.end())
            {
                cancelEQS(entity, it->second);
                pendingEQSQueries.erase(it);
            }
            break;
        }
        case BTNodeType::ScriptTask:
        {
            if (!scriptingProvider || node.scriptPath.empty()) break;

            auto sit = scriptInstances.find({entity.id, node.scriptPath});
            if (sit != scriptInstances.end() &&
                scriptingProvider->isScriptLoaded(sit->second) &&
                scriptingProvider->hasMethod(sit->second, "onAbort"))
            {
                scriptingProvider->callMethodWithReturn(sit->second, "onAbort");
            }
            break;
        }
        default:
            break;
        }
    }

    BTNodeStatus BehaviorTreeAdapter::executeLineOfSight(services::EntityHandle entity,
                                                          const std::string& targetKey,
                                                          float maxDistance,
                                                          float eyeOffset,
                                                          Blackboard& blackboard)
    {
        return computeLineOfSight(entity, targetKey, maxDistance, eyeOffset, blackboard)
                   ? BTNodeStatus::Success
                   : BTNodeStatus::Failure;
    }

    // === Shared sensing helpers (reused by task methods and built-in services) ===

    eqs::EQSContext BehaviorTreeAdapter::buildEQSContext(services::EntityHandle entity, const BTNode& node,
                                                         const std::string& targetKey, const Blackboard& blackboard)
    {
        eqs::EQSContext context;
        context.querierEntityId = entity.id;
        context.seed = eqsSeed(entity.id, node.id, tickCounter);

        auto& registry = scene::EntityRegistry::getRegistry();
        if (services::internal::isValidHandle(entity, registry))
        {
            const entt::entity querier = services::internal::fromHandle(entity);
            // Composed from this frame's local transforms (like worldPositionOf). The forward is the entity's
            // +Z on the ground plane - for a yaw-only rotation exactly (sin yaw, 0, cos yaw), as before.
            const glm::mat4 world = scene::composeWorldMatrix(registry, querier);
            context.querierPosition = glm::vec3(world[3]);
            const glm::vec3 forward(world[2].x, 0.0f, world[2].z);
            const float lengthSquared = glm::dot(forward, forward);
            if (lengthSquared > 1.0e-8f)
                context.querierForward = forward / std::sqrt(lengthSquared);

            // PathCost tests search paths for the querier's own size.
            if (const auto* agent = registry.try_get<components::NavmeshAgentComponent>(querier))
            {
                context.agentRadius = agent->radius;
                context.agentHeight = agent->height;
            }
        }

        if (!targetKey.empty())
        {
            const BlackboardTarget target = lookupTarget(targetKey, blackboard);
            if (target.lookup == TargetLookup::Found)
            {
                context.hasTarget = true;
                context.targetPosition = target.position;
                context.targetEntityId = target.entity.isValid() ? target.entity.id : eqs::kNoEntity;
            }
            else if ((target.lookup == TargetLookup::Missing || target.lookup == TargetLookup::WrongType) &&
                     warnOnce(entity.id, "eqs-target:" + targetKey))
            {
                vfLogWarning("BT EQS: target key '{}' {}", targetKey,
                             target.lookup == TargetLookup::Missing
                                 ? "is not on the blackboard"
                                 : "is not an Entity, Int (entity id) or Vec3 key");
            }
            // Unset (no current target): the query runs without one.
        }
        return context;
    }

    // VK-1660: the EQS calls never throw into the tick (a throw inside a frame task terminates the editor):
    // a failure becomes an invalid handle / a Failed result plus one warning per (entity, warnKey). A failed
    // submit shares its "{warnKey}:submit" entry with the caller's own warning, so it logs one line.
    eqs::EQSQueryHandle BehaviorTreeAdapter::submitEQS(services::EntityHandle entity, const std::string& queryName,
                                                       const eqs::EQSContext& context, const std::string& warnKey)
    {
        try
        {
            events::ai::SubmitEQSQueryCommand submitCmd;
            submitCmd.queryName = queryName;
            submitCmd.context = context;
            return events::EventDispatcher::instance().execute(submitCmd);
        }
        catch (const std::exception& e)
        {
            if (warnOnce(entity.id, warnKey + ":submit"))
                vfLogWarning("BT EQS: submitting '{}' for entity {} failed: {}", queryName, entity.id, e.what());
            return {};
        }
    }

    eqs::EQSResult BehaviorTreeAdapter::pollEQS(services::EntityHandle entity, const eqs::EQSQueryHandle& handle,
                                                const std::string& warnKey)
    {
        events::ai::GetEQSQueryResultQuery resultQuery;
        resultQuery.handle = handle;
        try
        {
            return events::EventDispatcher::instance().query(resultQuery);
        }
        catch (const std::exception& e)
        {
            if (warnOnce(entity.id, warnKey + ":poll-threw"))
                vfLogWarning("BT EQS: reading a query result for entity {} failed: {}", entity.id, e.what());
            eqs::EQSResult failed;
            failed.status = eqs::EQSQueryStatus::Failed;
            return failed;
        }
    }

    void BehaviorTreeAdapter::writeEQSResult(services::EntityHandle entity, Blackboard& blackboard,
                                             const eqs::EQSResult& result, const std::string& resultKey,
                                             const std::string& resultEntityKey)
    {
        const EQSResultWrite write = applyEQSResult(blackboard, result, resultKey, resultEntityKey);
        if ((write.entity == EQSKeyWrite::Missing || write.entity == EQSKeyWrite::WrongType) &&
            warnOnce(entity.id, "eqs-entity-key:" + resultEntityKey))
        {
            vfLogWarning("BT EQS: result entity key '{}' {}", resultEntityKey,
                         write.entity == EQSKeyWrite::Missing ? "is not on the blackboard"
                                                              : "is not an Entity or Int key");
        }
    }

    void BehaviorTreeAdapter::cancelEQS(services::EntityHandle entity, const eqs::EQSQueryHandle& handle)
    {
        events::ai::CancelEQSQueryCommand cancelCmd;
        cancelCmd.handle = handle;
        try
        {
            events::EventDispatcher::instance().execute(cancelCmd);
        }
        catch (const std::exception& e)
        {
            if (warnOnce(entity.id, "eqs:cancel-threw"))
                vfLogWarning("BT EQS: cancelling a query for entity {} failed: {}", entity.id, e.what());
        }
    }

    std::optional<glm::vec3> BehaviorTreeAdapter::worldPositionOf(services::EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!services::internal::isValidHandle(entity, registry))
            return std::nullopt;
        return scene::composeWorldPosition(registry, services::internal::fromHandle(entity));
    }

    BehaviorTreeAdapter::BlackboardTarget BehaviorTreeAdapter::lookupTarget(const std::string& targetKey,
                                                                           const Blackboard& blackboard) const
    {
        BlackboardTarget target;
        if (!blackboard.has(targetKey))
            return target; // Missing

        const BlackboardValue value = blackboard.get(targetKey);
        if (std::holds_alternative<glm::vec3>(value))
        {
            target.lookup = TargetLookup::Found;
            target.position = std::get<glm::vec3>(value);
            return target;
        }

        services::EntityHandle entity = services::EntityHandle::invalid();
        if (std::holds_alternative<services::EntityHandle>(value))
        {
            entity = std::get<services::EntityHandle>(value);
        }
        else if (std::holds_alternative<int32_t>(value))
        {
            // Scripts keep entity ids in Int keys, -1 = none. The id is the entt entity's 32 bits (an int32
            // key wraps ids >= 2^31 negative, so read the bits back unsigned); -1 is entt::null.
            const uint32_t bits = static_cast<uint32_t>(std::get<int32_t>(value));
            if (bits != 0xFFFFFFFFu)
                entity = services::EntityHandle{static_cast<uint64_t>(bits)};
        }
        else
        {
            target.lookup = TargetLookup::WrongType;
            return target;
        }

        const auto position = worldPositionOf(entity);
        if (!position.has_value())
        {
            target.lookup = TargetLookup::Unset;
            return target;
        }
        target.lookup = TargetLookup::Found;
        target.position = *position;
        target.entity = entity;
        return target;
    }

    std::optional<glm::vec3> BehaviorTreeAdapter::resolveTargetPosition(const std::string& targetKey,
                                                                        const BlackboardTarget& target,
                                                                        float eyeOffset) const
    {
        if (target.lookup == TargetLookup::WrongType)
        {
            vfLogWarning("BT: blackboard key '{}' is not Entity, Int (entity id) or Vec3", targetKey);
            return std::nullopt;
        }
        if (target.lookup != TargetLookup::Found)
            return std::nullopt;
        // An entity is aimed at eyeOffset above its pivot; a Vec3 is the point itself.
        if (target.entity.isValid())
            return target.position + glm::vec3(0.0f, eyeOffset, 0.0f);
        return target.position;
    }

    bool BehaviorTreeAdapter::computeLineOfSight(services::EntityHandle entity,
                                                 const std::string& targetKey,
                                                 float maxDistance,
                                                 float eyeOffset,
                                                 Blackboard& blackboard) const
    {
        if (!blackboard.has(targetKey))
        {
            vfLogWarning("BT LineOfSight: blackboard key '{}' not found", targetKey);
            return false;
        }

        const auto ownerPosition = worldPositionOf(entity);
        if (!ownerPosition.has_value())
            return false;

        glm::vec3 origin = *ownerPosition + glm::vec3(0.0f, eyeOffset, 0.0f);

        const BlackboardTarget target = lookupTarget(targetKey, blackboard);
        auto targetPosOpt = resolveTargetPosition(targetKey, target, eyeOffset);
        if (!targetPosOpt.has_value())
            return false;

        glm::vec3 toTarget = *targetPosOpt - origin;
        float distance = glm::length(toTarget);

        if (distance > maxDistance || distance < 0.001f)
            return false;

        glm::vec3 direction = toTarget / distance;

        // VK-1660: every hit along the ray, so the looker's own collider (which the ray starts inside:
        // Jolt reports convex shapes as solid, a hit at distance 0) and the target's collider can be told
        // apart from real obstacles.
        events::physics::RaycastAllQuery rayQuery;
        rayQuery.origin = origin;
        rayQuery.direction = direction;
        rayQuery.maxDistance = distance;
        const std::vector<services::RaycastHit> hits = events::EventDispatcher::instance().query(rayQuery);

        const auto& registry = scene::EntityRegistry::getRegistry();
        std::vector<SightHit> sightHits;
        sightHits.reserve(hits.size());
        for (const services::RaycastHit& hit : hits)
        {
            if (!hit.hit)
                continue;
            SightHit sightHit;
            sightHit.distance = hit.distance;
            if (isSelfOrDescendant(registry, hit.entity, entity))
                sightHit.kind = SightHitKind::Ignored;
            else if (isSelfOrDescendant(registry, hit.entity, target.entity))
                sightHit.kind = SightHitKind::Target;
            else
                sightHit.kind = SightHitKind::Blocker;
            sightHits.push_back(sightHit);
        }
        return isSightLineClear(std::move(sightHits), distance, kSightTolerance);
    }

    // === Service hooks (VK-1456) ===

    void BehaviorTreeAdapter::onServiceTick(services::EntityHandle entity, const BTNode& node,
                                            Blackboard& blackboard, float deltaTime)
    {
        (void)deltaTime;
        const std::string serviceType = getNodeProperty<std::string>(node, "serviceType", std::string("EQSRefresh"));

        if (serviceType == "EQSRefresh")
        {
            const std::string queryName = getNodeProperty<std::string>(node, "queryName", std::string{});
            const std::string resultKey = getNodeProperty<std::string>(node, "resultKey", std::string("eqsResult"));
            const std::string targetKey = getNodeProperty<std::string>(node, "targetKey", std::string{});
            const std::string resultEntityKey = getNodeProperty<std::string>(node, "resultEntityKey", std::string{});
            if (queryName.empty())
                return;

            const std::string key = eqsServiceKey(entity.id, node);
            const std::string warnKey = "eqs-service:" + key;
            auto it = pendingEQSQueries.find(key);
            if (it != pendingEQSQueries.end())
            {
                const eqs::EQSResult result = pollEQS(entity, it->second, warnKey);
                if (result.status == eqs::EQSQueryStatus::Pending || result.status == eqs::EQSQueryStatus::Running)
                    return; // still in flight — wait for it before resubmitting

                // Finished: free the result at once, then write it (VK-1660: none found clears resultEntityKey).
                cancelEQS(entity, it->second);
                pendingEQSQueries.erase(it);
                if (result.status == eqs::EQSQueryStatus::Completed)
                {
                    writeEQSResult(entity, blackboard, result, resultKey, resultEntityKey);
                }
                else if (!result.error.empty() && warnOnce(entity.id, warnKey + ":failed"))
                {
                    vfLogWarning("BT EQSRefresh '{}': query '{}' failed for entity {}: {}",
                                 node.name, queryName, entity.id, result.error);
                }
            }

            const eqs::EQSContext context = buildEQSContext(entity, node, targetKey, blackboard);
            auto handle = submitEQS(entity, queryName, context, warnKey);
            if (handle.isValid())
            {
                pendingEQSQueries[key] = handle;
            }
            else if (warnOnce(entity.id, warnKey + ":submit"))
            {
                vfLogWarning("BT EQSRefresh '{}': failed to submit query '{}' for entity {} (no valid "
                             "assets/eqs/{}.vfEQSQuery, or the queue is full)",
                             node.name, queryName, entity.id, queryName);
            }
        }
        else if (serviceType == "LineOfSightRefresh")
        {
            const std::string targetKey = getNodeProperty<std::string>(node, "targetKey", std::string("target"));
            const std::string visibilityKey = getNodeProperty<std::string>(node, "visibilityKey", std::string("targetVisible"));
            const float maxDistance = getNodeProperty<float>(node, "maxDistance", 50.0f);
            const float eyeOffset = getNodeProperty<float>(node, "eyeOffset", 1.6f);
            const bool visible = computeLineOfSight(entity, targetKey, maxDistance, eyeOffset, blackboard);
            blackboard.set(visibilityKey, visible);
        }
        else if (serviceType == "FocusUpdate")
        {
            const std::string targetKey = getNodeProperty<std::string>(node, "targetKey", std::string("target"));
            const std::string focusKey = getNodeProperty<std::string>(node, "focusKey", std::string("focusPoint"));
            auto pos = resolveTargetPosition(targetKey, lookupTarget(targetKey, blackboard), 0.0f);
            if (pos.has_value())
                blackboard.set(focusKey, *pos);
        }
        else if (serviceType == "TargetDistance")
        {
            // VK-1660: owner-to-target distance for range checks ("targetDistance <= 10" in a
            // BlackboardCondition), kNoTargetDistance when there is no target.
            const std::string targetKey = getNodeProperty<std::string>(node, "targetKey", std::string("target"));
            const std::string distanceKey = getNodeProperty<std::string>(node, "distanceKey", std::string("targetDistance"));
            const bool horizontal = getNodeProperty<bool>(node, "horizontal", true);
            if (distanceKey.empty())
                return;

            float distance = kNoTargetDistance;
            const BlackboardTarget target = lookupTarget(targetKey, blackboard);
            const auto ownerPosition = worldPositionOf(entity);
            if (target.lookup == TargetLookup::Found && ownerPosition.has_value())
            {
                glm::vec3 delta = target.position - *ownerPosition;
                if (horizontal)
                    delta.y = 0.0f;
                distance = glm::length(delta);
            }
            else if (target.lookup == TargetLookup::WrongType &&
                     warnOnce(entity.id, "targetdistance:" + targetKey))
            {
                vfLogWarning("BT TargetDistance: blackboard key '{}' is not Entity, Int (entity id) or Vec3",
                             targetKey);
            }
            blackboard.set(distanceKey, distance);
        }
    }

    void BehaviorTreeAdapter::onServiceEnd(services::EntityHandle entity, const BTNode& node,
                                           Blackboard& blackboard)
    {
        (void)blackboard;
        // Only EQSRefresh holds in-flight async work to cancel. Sensory result keys are left intact
        // so downstream logic keeps the last known value after the branch deactivates.
        const std::string serviceType = getNodeProperty<std::string>(node, "serviceType", std::string("EQSRefresh"));
        if (serviceType != "EQSRefresh")
            return;

        const std::string key = eqsServiceKey(entity.id, node);
        auto it = pendingEQSQueries.find(key);
        if (it != pendingEQSQueries.end())
        {
            cancelEQS(entity, it->second);
            pendingEQSQueries.erase(it);
        }
    }

    // === FollowRoute (VK-1660) ===

    void BehaviorTreeAdapter::stopAgent(services::EntityHandle entity)
    {
        if (isVolumetricAgent(entity))
        {
            try
            {
                events::volumetric::StopVolumetricAgentCommand stopCmd;
                stopCmd.entity = entity;
                events::EventDispatcher::instance().execute(stopCmd);
            }
            catch (const std::exception& e)
            {
                if (warnOnce(entity.id, "flight:stop"))
                    vfLogWarning("BT: stopping flying agent {} failed: {}", entity.id, e.what());
            }
            return;
        }

        events::navmesh::StopAgentCommand stopCmd;
        stopCmd.entity = entity;
        events::EventDispatcher::instance().execute(stopCmd);
    }

    // === Flying agents (VK-1660) ===

    bool BehaviorTreeAdapter::isVolumetricAgent(services::EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!services::internal::isValidHandle(entity, registry))
            return false;
        const entt::entity owner = services::internal::fromHandle(entity);
        return registry.all_of<components::VolumetricAgentComponent>(owner) &&
               !registry.all_of<components::NavmeshAgentComponent>(owner);
    }

    void BehaviorTreeAdapter::setVolumetricDestination(services::EntityHandle entity, const glm::vec3& target)
    {
        try
        {
            events::volumetric::SetVolumetricAgentDestinationCommand cmd;
            cmd.entity = entity;
            cmd.target = target;
            events::EventDispatcher::instance().execute(cmd);
        }
        catch (const std::exception& e)
        {
            if (warnOnce(entity.id, "flight:destination"))
                vfLogWarning("BT: sending flying agent {} to a destination failed: {}", entity.id, e.what());
        }
    }

    std::optional<services::VolumetricAgentState> BehaviorTreeAdapter::volumetricAgentState(
        services::EntityHandle entity)
    {
        try
        {
            events::volumetric::GetVolumetricAgentStateQuery query;
            query.entity = entity;
            return events::EventDispatcher::instance().query(query);
        }
        catch (const std::exception& e)
        {
            if (warnOnce(entity.id, "flight:state"))
                vfLogWarning("BT: reading flying agent {}'s state failed: {}", entity.id, e.what());
            return std::nullopt;
        }
    }

    glm::vec3 BehaviorTreeAdapter::snapToVolume(const glm::vec3& point) const
    {
        // No volume near the marker (or no handler): keep it as placed - the destination command that follows
        // reports the failure through the agent's state.
        try
        {
            events::volumetric::GetNearestNavigable3DQuery query;
            query.point = point;
            query.snapRadius = kRouteVolumeSnapRadius;
            if (const std::optional<glm::vec3> nearest = events::EventDispatcher::instance().query(query))
                return *nearest;
        }
        catch (const std::exception&)
        {
        }
        return point;
    }

    glm::vec3 BehaviorTreeAdapter::snapToNavmesh(const glm::vec3& point) const
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::navmesh::IsPointOnNavmeshQuery onMesh;
        onMesh.point = point;
        onMesh.tolerance = kRouteSnapTolerance;
        if (dispatcher.query(onMesh))
            return point;

        // GetClosestPoint echoes its input when nothing is found, so every candidate is re-checked.
        for (float radius : kRouteSnapRadii)
        {
            events::navmesh::GetClosestPointQuery closest;
            closest.point = point;
            closest.searchRadius = radius;
            const glm::vec3 candidate = dispatcher.query(closest);

            events::navmesh::IsPointOnNavmeshQuery check;
            check.point = candidate;
            check.tolerance = kRouteSnapTolerance;
            if (dispatcher.query(check))
                return candidate;
        }
        return point; // no navmesh near the marker: keep it as placed
    }

    std::optional<std::vector<glm::vec3>> BehaviorTreeAdapter::gatherRoutePoints(services::EntityHandle entity,
                                                                                const std::string& routeKey,
                                                                                const Blackboard& blackboard,
                                                                                std::string& problem) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        const bool flying = isVolumetricAgent(entity);
        if (!services::internal::isValidHandle(entity, registry) ||
            (!flying && !registry.all_of<components::NavmeshAgentComponent>(services::internal::fromHandle(entity))))
        {
            problem = "the entity has no NavmeshAgent or VolumetricAgent";
            return std::nullopt;
        }

        const BlackboardTarget route = lookupTarget(routeKey, blackboard);
        if (route.lookup == TargetLookup::Missing)
        {
            problem = "blackboard key '" + routeKey + "' not found";
            return std::nullopt;
        }
        if (route.lookup == TargetLookup::WrongType || (route.lookup == TargetLookup::Found && !route.entity.isValid()))
        {
            problem = "blackboard key '" + routeKey + "' must hold the route entity (Entity, or Int entity id)";
            return std::nullopt;
        }
        if (route.lookup != TargetLookup::Found)
        {
            problem = "blackboard key '" + routeKey + "' holds no live route entity";
            return std::nullopt;
        }

        // The markers are the route's children in hierarchy (ChildrenComponent) order.
        const entt::entity routeEntity = services::internal::fromHandle(route.entity);
        std::vector<glm::vec3> points;
        if (const auto* children = registry.try_get<components::ChildrenComponent>(routeEntity))
        {
            points.reserve(children->children.size());
            for (const entt::entity child : children->children)
            {
                if (child == entt::null || !registry.valid(child))
                    continue;
                const glm::vec3 marker = scene::composeWorldPosition(registry, child);
                points.push_back(flying ? snapToVolume(marker) : snapToNavmesh(marker));
            }
        }
        if (points.empty())
        {
            problem = "the route entity has no marker children";
            return std::nullopt;
        }
        return points;
    }

    BTNodeStatus BehaviorTreeAdapter::executeFollowRoute(services::EntityHandle entity,
                                                          const BTNode& node,
                                                          const FollowRouteRequest& request,
                                                          Blackboard& blackboard,
                                                          float deltaTime,
                                                          bool isFirstTick)
    {
        const NodeRunKey key{entity.id, &node};
        const std::string warnKey = "route:" + std::to_string(reinterpret_cast<uintptr_t>(&node));
        auto runIt = routeRuns.find(key);
        // Progress dropped while the node kept running (disable, reload) re-enters like a first tick.
        const bool entering = isFirstTick || runIt == routeRuns.end();

        if (entering)
        {
            // Markers are read once per entry: moving one takes effect the next time the node starts.
            std::string problem;
            auto points = gatherRoutePoints(entity, request.routeKey, blackboard, problem);
            if (!points.has_value())
            {
                if (warnOnce(entity.id, warnKey))
                    vfLogWarning("BT FollowRoute '{}' (entity {}): {}", node.name, entity.id, problem);
                return BTNodeStatus::Failure;
            }
            forgetWarning(entity.id, warnKey);
            if (runIt == routeRuns.end())
                runIt = routeRuns.emplace(key, RouteRun{}).first;
            runIt->second.points = std::move(*points);
        }

        const auto ownerPosition = worldPositionOf(entity);
        if (!ownerPosition.has_value())
            return BTNodeStatus::Failure;

        // A flying agent walks the markers in 3D through the voxel volumes.
        const bool flying = isVolumetricAgent(entity);
        RouteFollowerSettings settings;
        settings.mode = request.mode;
        settings.arrivalDistance = request.arrivalDistance;
        settings.dwell = request.dwell;
        settings.startAtNearest = request.startAtNearest;
        settings.legTimeout = request.legTimeout;
        settings.horizontal = !flying;

        RouteRun& run = runIt->second;
        const RouteStep step = stepRoute(run.points, settings, run.follower, *ownerPosition, deltaTime, entering);
        switch (step.action)
        {
        case RouteAction::IssueDestination:
        {
            const glm::vec3& marker = run.points[static_cast<size_t>(step.targetIndex)];
            if (flying)
            {
                setVolumetricDestination(entity, marker);
            }
            else
            {
                events::navmesh::SetAgentDestinationCommand navCmd;
                navCmd.entity = entity;
                navCmd.target = marker;
                events::EventDispatcher::instance().execute(navCmd);
            }
            break;
        }
        case RouteAction::Stop:
            stopAgent(entity);
            break;
        case RouteAction::None:
            break;
        }

        // A flying agent that cannot reach its marker (no path through the volumes, stuck) fails the patrol, as a
        // timed-out leg does. Not read on a tick that issued a leg: from the next tick on the state is that leg's.
        if (flying && step.status == BTNodeStatus::Running && step.action != RouteAction::IssueDestination)
        {
            const auto state = volumetricAgentState(entity);
            if (state && state->state == services::VolumetricAgentMoveState::Failed)
            {
                if (warnOnce(entity.id, warnKey + ":flight"))
                {
                    vfLogWarning("BT FollowRoute '{}' (entity {}): the flying agent cannot reach marker {}: {}",
                                 node.name, entity.id, run.follower.targetIndex, state->failReason);
                }
                stopAgent(entity);
                return BTNodeStatus::Failure;
            }
        }
        return step.status;
    }

    // === Per-entity task state (VK-1660) ===

    void BehaviorTreeAdapter::clearMovementState(uint64_t entityId)
    {
        moveTrackers.erase(entityId);
        for (auto it = routeRuns.begin(); it != routeRuns.end(); )
        {
            if (it->first.entityId == entityId)
                it = routeRuns.erase(it);
            else
                ++it;
        }
    }

    void BehaviorTreeAdapter::clearEntityTaskState(uint64_t entityId)
    {
        clearMovementState(entityId);
        for (auto it = failedScriptLoads.begin(); it != failedScriptLoads.end(); )
        {
            if (it->entityId == entityId)
                it = failedScriptLoads.erase(it);
            else
                ++it;
        }
        const std::string prefix = std::to_string(entityId) + ":";
        for (auto it = loggedWarnings.begin(); it != loggedWarnings.end(); )
        {
            if (it->compare(0, prefix.size(), prefix) == 0)
                it = loggedWarnings.erase(it);
            else
                ++it;
        }
    }

    bool BehaviorTreeAdapter::warnOnce(uint64_t entityId, const std::string& what)
    {
        return loggedWarnings.insert(std::to_string(entityId) + ":" + what).second;
    }

    void BehaviorTreeAdapter::forgetWarning(uint64_t entityId, const std::string& what)
    {
        loggedWarnings.erase(std::to_string(entityId) + ":" + what);
    }
}

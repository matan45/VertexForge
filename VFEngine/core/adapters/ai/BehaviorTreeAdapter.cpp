#include "BehaviorTreeAdapter.hpp"
#include "../../../services/providers/scripting/IScriptingProvider.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/navmesh/NavmeshEvents.hpp"
#include "../../../services/events/physics/ControllerEvents.hpp"
#include "../../../services/events/animation/AnimatorEvents.hpp"
#include "../../../services/events/ai/EQSEvents.hpp"
#include "../../../services/events/scene/EntityTransformEvents.hpp"
#include "../../../services/events/physics/PhysicsEvents.hpp"
#include "print/Log.hpp"
#include "behaviortree/BehaviorTreeValidation.hpp"
#include <algorithm>

namespace core
{
    
    using namespace behaviortree;

    BehaviorTreeAdapter::BehaviorTreeAdapter(services::IScriptingProvider* scriptingProvider)
        : scriptingProvider(scriptingProvider)
    {
    }

    BehaviorTreeAdapter::~BehaviorTreeAdapter() = default;

    static std::string normalizeBTPath(std::string path)
    {
        std::replace(path.begin(), path.end(), '\\', '/');
        return path;
    }

    template <typename T>
    static T getServiceProp(const BTNode& node, const std::string& key, T defaultVal)
    {
        auto it = node.properties.find(key);
        if (it != node.properties.end() && std::holds_alternative<T>(it->second))
            return std::get<T>(it->second);
        return defaultVal;
    }

    // Service EQS queries key off the node id so multiple EQS services on one entity don't collide;
    // the "{entityId}:" prefix means cancelPendingEQSQueriesForEntity() still sweeps them.
    static std::string eqsServiceKey(uint64_t entityId, uint32_t nodeId)
    {
        return std::to_string(entityId) + ":svc:" + std::to_string(nodeId);
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
        auto cacheIt = assetCache.find(treePath);
        if (cacheIt != assetCache.end())
            return cacheIt->second;

        std::vector<std::string> dependencies;
        auto dataOpt = BehaviorTreeAsset::loadExpanded(treePath, &dependencies);
        if (!dataOpt.has_value())
            return nullptr;
        if (!validateExpandedOrLog(dataOpt.value(), treePath))
            return nullptr;

        auto shared = std::make_shared<const BehaviorTreeData>(std::move(dataOpt.value()));
        assetCache[treePath] = shared;
        assetDependencies[treePath] = std::move(dependencies);
        return shared;
    }

    bool BehaviorTreeAdapter::attachTree(services::EntityHandle entity, const std::string& treePath)
    {
        if (!entity.isValid() || treePath.empty())
            return false;

        auto treeData = getOrLoadTree(treePath);
        if (!treeData)
        {
            vfLogError("Failed to load behavior tree: {}", treePath);
            return false;
        }

        detachTree(entity);

        RuntimeInstance instance;
        instance.runtime = std::make_unique<BehaviorTreeRuntime>();
        instance.runtime->init(treeData, entity);
        instance.treePath = treePath;
        instance.enabled = true;

        runtimes[entity.id] = std::move(instance);
        vfLogInfo("Attached behavior tree '{}' to entity {}", treePath, entity.id);
        return true;
    }

    void BehaviorTreeAdapter::cleanupScriptInstances(uint64_t entityId, const BehaviorTreeData& treeData)
    {
        if (!scriptingProvider) return;

        for (const auto& node : treeData.graph.nodes)
        {
            if (node.type != BTNodeType::ScriptTask || node.scriptPath.empty())
                continue;

            auto sit = scriptInstances.find({entityId, node.scriptPath});
            if (sit != scriptInstances.end())
            {
                if (scriptingProvider->isScriptLoaded(sit->second))
                {
                    scriptingProvider->callOnDestroy(sit->second);
                    scriptingProvider->unloadScript(sit->second);
                }
                scriptInstances.erase(sit);
            }
        }
    }

    void BehaviorTreeAdapter::cancelPendingEQSQueriesForEntity(uint64_t entityId)
    {
        std::string prefix = std::to_string(entityId) + ":";
        for (auto eqsIt = pendingEQSQueries.begin(); eqsIt != pendingEQSQueries.end(); )
        {
            if (eqsIt->first.compare(0, prefix.size(), prefix) == 0)
            {
                events::ai::CancelEQSQueryCommand cancelCmd;
                cancelCmd.handle = eqsIt->second;
                events::EventDispatcher::instance().execute(cancelCmd);
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

        if (it->second.runtime && it->second.runtime->hasTreeData())
        {
            cleanupScriptInstances(entity.id, it->second.runtime->getTreeData());
        }
        if (it->second.runtime)
        {
            it->second.runtime->endAllServices(this);
        }
        cancelPendingEQSQueriesForEntity(entity.id);

        runtimes.erase(it);
    }

    void BehaviorTreeAdapter::setEnabled(services::EntityHandle entity, bool enabled)
    {
        auto it = runtimes.find(entity.id);
        if (it != runtimes.end())
        {
            // A disabled runtime stops being ticked, so flush its services now (the end-of-tick
            // diff can't run while it's not ticking).
            if (!enabled && it->second.runtime)
                it->second.runtime->endAllServices(this);
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

        std::lock_guard<std::mutex> lock(reloadMutex);
        for (const auto& pending : pendingReloads)
        {
            if (pending == treePath) return;
        }
        pendingReloads.push_back(treePath);
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

        for (const auto& savedPath : reloads)
        {
            addRoot(savedPath);

            std::string normalized = normalizeBTPath(savedPath);
            for (const auto& [rootPath, dependencies] : assetDependencies)
            {
                if (rootPath == savedPath) continue;
                if (std::find(dependencies.begin(), dependencies.end(), normalized) != dependencies.end())
                    addRoot(rootPath);
            }
        }

        for (const auto& treePath : rootsToReload)
        {
            std::vector<std::string> dependencies;
            auto dataOpt = BehaviorTreeAsset::loadExpanded(treePath, &dependencies);
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

            auto newData = std::make_shared<const BehaviorTreeData>(std::move(dataOpt.value()));
            assetCache[treePath] = newData;
            assetDependencies[treePath] = std::move(dependencies);

            int rebound = 0;
            for (auto& [entityId, instance] : runtimes)
            {
                if (instance.treePath != treePath || !instance.runtime) continue;

                // Old node ids/scripts are invalid against the new graph
                if (instance.runtime->hasTreeData())
                {
                    cleanupScriptInstances(entityId, instance.runtime->getTreeData());
                }
                cancelPendingEQSQueriesForEntity(entityId);
                instance.runtime->endAllServices(this);

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

    void BehaviorTreeAdapter::captureDebugSnapshot(const BehaviorTreeRuntime& runtime)
    {
        BTRuntimeSnapshot snapshot;
        snapshot.valid = true;
        snapshot.tickIndex = tickCounter;

        const auto& nodeStates = runtime.getNodeStates();
        snapshot.nodeStatuses.reserve(nodeStates.size());
        for (const auto& [nodeId, state] : nodeStates)
            snapshot.nodeStatuses[nodeId] = state.lastStatus;

        const auto& blackboardValues = runtime.getBlackboard().getAll();
        snapshot.blackboard.reserve(blackboardValues.size());
        for (const auto& [key, value] : blackboardValues)
            snapshot.blackboard.emplace_back(key, value);
        std::sort(snapshot.blackboard.begin(), snapshot.blackboard.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        std::lock_guard<std::mutex> lock(snapshotMutex);
        debugSnapshot = std::move(snapshot);
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

        for (uint64_t entityId : entityIds)
        {
            auto it = runtimes.find(entityId);
            if (it == runtimes.end()) continue;

            auto& instance = it->second;
            if (!instance.enabled || !instance.runtime) continue;

            instance.lastTickStatus = instance.runtime->tick(deltaTime, this);

            if (entityId == debugId)
                captureDebugSnapshot(*instance.runtime);
        }
    }

    void BehaviorTreeAdapter::setDebugTarget(services::EntityHandle entity)
    {
        debugTargetEntityId.store(entity.isValid() ? entity.id : 0);

        std::lock_guard<std::mutex> lock(snapshotMutex);
        debugSnapshot = {};
    }

    BTRuntimeSnapshot BehaviorTreeAdapter::getRuntimeSnapshot(services::EntityHandle entity) const
    {
        if (!entity.isValid() || entity.id != debugTargetEntityId.load(std::memory_order_relaxed))
            return {};

        std::lock_guard<std::mutex> lock(snapshotMutex);
        return debugSnapshot;
    }

    void BehaviorTreeAdapter::stopAll()
    {
        for (auto& [_, instance] : runtimes)
        {
            if (instance.runtime)
            {
                instance.runtime->endAllServices(this);
                instance.runtime->reset();
            }
        }
        // Play session over: drop cached assets so the next session reloads from disk
        assetCache.clear();
        assetDependencies.clear();

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
                                                     const std::string& targetKey,
                                                     float arrivalDistance,
                                                     Blackboard& blackboard,
                                                     bool isFirstTick)
    {
        if (!blackboard.has(targetKey))
        {
            vfLogWarning("BT MoveTo: blackboard key '{}' not found", targetKey);
            return BTNodeStatus::Failure;
        }

        auto& dispatcher = events::EventDispatcher::instance();
        glm::vec3 target = blackboard.getVec3(targetKey);

        if (isFirstTick)
        {
            events::controller::SetArrivalDistanceCommand arrivalCmd;
            arrivalCmd.entity = entity;
            arrivalCmd.arrivalDistance = arrivalDistance;
            dispatcher.execute(arrivalCmd);

            events::navmesh::SetAgentDestinationCommand navCmd;
            navCmd.entity = entity;
            navCmd.target = target;
            dispatcher.execute(navCmd);

            // Defer the arrival check by one tick. The navmesh agent updates AFTER the
            // behavior tree in the frame graph (Controllers -> BehaviorTrees -> Navmesh),
            // so HasReachedDestination still reflects the PREVIOUS destination this frame.
            // Checking it now would wrongly report "arrived" the instant a new move is
            // issued (e.g. the return-home leg right after reaching the resource),
            // collapsing the loop and pinning the agent in place. Let the agent act on
            // the new destination first; arrival is evaluated on the next tick onward.
            return BTNodeStatus::Running;
        }

        // Arrival check by horizontal distance to the target. MoveTo issues the
        // destination through the navmesh agent (SetAgentDestinationCommand above), but
        // the controller-based HasReachedDestinationQuery only works for entities that
        // have a ControllerComponent and returns true for everything else (e.g. a
        // NavmeshAgent-driven unit), which collapses the loop. A direct distance test
        // works for any movement system (mirrors the legacy harvester's arrived()).
        glm::vec3 pos = target;
        events::scene::GetWorldTransformQuery transformQuery;
        transformQuery.entity = entity;
        auto transformOpt = dispatcher.query(transformQuery);
        if (transformOpt.has_value())
            pos = transformOpt->position;

        float dx = target.x - pos.x;
        float dz = target.z - pos.z;
        bool reached = (dx * dx + dz * dz) <= (arrivalDistance * arrivalDistance);
        return reached ? BTNodeStatus::Success : BTNodeStatus::Running;
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
            auto info = scriptingProvider->loadScript(scriptPath, entity);
            if (!info.has_value())
            {
                vfLogWarning("BT ScriptTask: failed to load script '{}' for entity {}", scriptPath, entity.id);
                return BTNodeStatus::Failure;
            }
            scriptInstances[scriptKey] = info->instanceId;
            scriptingProvider->callOnStart(info->instanceId);
            it = scriptInstances.find(scriptKey);
        }

        std::string result = scriptingProvider->callMethodWithReturn(
            it->second, "tick", {std::any(deltaTime)});

        if (result == "success") return BTNodeStatus::Success;
        if (result == "running") return BTNodeStatus::Running;
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
        const std::string& queryName,
        const std::string& resultKey,
        Blackboard& blackboard,
        bool isFirstTick)
    {
        if (queryName.empty())
        {
            vfLogWarning("BT EnvironmentQuery: empty query name for entity {}", entity.id);
            return BTNodeStatus::Failure;
        }

        if (isFirstTick)
        {
            auto handle = submitEQS(entity, queryName);
            if (!handle.isValid())
            {
                vfLogWarning("BT EnvironmentQuery: failed to submit query '{}' for entity {}", queryName, entity.id);
                return BTNodeStatus::Failure;
            }

            std::string eqsKey = std::to_string(entity.id) + ":" + queryName;
            pendingEQSQueries[eqsKey] = handle;
            return BTNodeStatus::Running;
        }

        // Poll for results on subsequent ticks
        std::string eqsKey = std::to_string(entity.id) + ":" + queryName;
        auto it = pendingEQSQueries.find(eqsKey);
        if (it == pendingEQSQueries.end())
        {
            return BTNodeStatus::Failure;
        }

        eqs::EQSResult result = pollEQS(it->second);

        switch (result.status)
        {
        case eqs::EQSQueryStatus::Completed:
        {
            pendingEQSQueries.erase(it);
            if (result.hasResults())
            {
                blackboard.set(resultKey, result.getBestPosition());
                return BTNodeStatus::Success;
            }
            return BTNodeStatus::Failure;
        }
        case eqs::EQSQueryStatus::Failed:
        {
            pendingEQSQueries.erase(it);
            return BTNodeStatus::Failure;
        }
        case eqs::EQSQueryStatus::Pending:
        case eqs::EQSQueryStatus::Running:
        default:
            return BTNodeStatus::Running;
        }
    }

    void BehaviorTreeAdapter::onAbort(services::EntityHandle entity, const BTNode& node)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        switch (node.type)
        {
        case BTNodeType::MoveTo:
        {
            events::navmesh::StopAgentCommand stopCmd;
            stopCmd.entity = entity;
            dispatcher.execute(stopCmd);
            break;
        }
        case BTNodeType::EnvironmentQuery:
        {
            std::string queryName;
            auto qIt = node.properties.find("queryName");
            if (qIt != node.properties.end() && std::holds_alternative<std::string>(qIt->second))
                queryName = std::get<std::string>(qIt->second);
            if (queryName.empty()) break;

            std::string eqsKey = std::to_string(entity.id) + ":" + queryName;
            auto it = pendingEQSQueries.find(eqsKey);
            if (it != pendingEQSQueries.end())
            {
                cancelEQS(it->second);
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

    eqs::EQSContext BehaviorTreeAdapter::buildEQSContext(services::EntityHandle entity) const
    {
        eqs::EQSContext context;
        context.querierEntityId = entity.id;

        events::scene::GetWorldTransformQuery transformQuery;
        transformQuery.entity = entity;
        auto transformOpt = events::EventDispatcher::instance().query(transformQuery);
        if (transformOpt.has_value())
        {
            context.querierPosition = transformOpt->position;
            // Derive forward from rotation (Y-axis euler rotation)
            float yaw = glm::radians(transformOpt->rotation.y);
            context.querierForward = glm::vec3(std::sin(yaw), 0.0f, std::cos(yaw));
        }
        return context;
    }

    eqs::EQSQueryHandle BehaviorTreeAdapter::submitEQS(services::EntityHandle entity, const std::string& queryName) const
    {
        events::ai::SubmitEQSQueryCommand submitCmd;
        submitCmd.queryName = queryName;
        submitCmd.context = buildEQSContext(entity);
        return events::EventDispatcher::instance().execute(submitCmd);
    }

    eqs::EQSResult BehaviorTreeAdapter::pollEQS(const eqs::EQSQueryHandle& handle) const
    {
        events::ai::GetEQSQueryResultQuery resultQuery;
        resultQuery.handle = handle;
        return events::EventDispatcher::instance().query(resultQuery);
    }

    void BehaviorTreeAdapter::cancelEQS(const eqs::EQSQueryHandle& handle) const
    {
        events::ai::CancelEQSQueryCommand cancelCmd;
        cancelCmd.handle = handle;
        events::EventDispatcher::instance().execute(cancelCmd);
    }

    std::optional<glm::vec3> BehaviorTreeAdapter::resolveTargetPosition(const std::string& targetKey,
                                                                        Blackboard& blackboard,
                                                                        float eyeOffset) const
    {
        if (!blackboard.has(targetKey))
            return std::nullopt;

        auto val = blackboard.get(targetKey);
        if (std::holds_alternative<services::EntityHandle>(val))
        {
            events::scene::GetWorldTransformQuery targetTransformQuery;
            targetTransformQuery.entity = std::get<services::EntityHandle>(val);
            auto targetTransform = events::EventDispatcher::instance().query(targetTransformQuery);
            if (!targetTransform.has_value())
                return std::nullopt;
            return targetTransform->position + glm::vec3(0.0f, eyeOffset, 0.0f);
        }
        if (std::holds_alternative<glm::vec3>(val))
        {
            return std::get<glm::vec3>(val);
        }

        vfLogWarning("BT: blackboard key '{}' is not Entity or Vec3", targetKey);
        return std::nullopt;
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

        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::GetWorldTransformQuery ownerTransformQuery;
        ownerTransformQuery.entity = entity;
        auto ownerTransform = dispatcher.query(ownerTransformQuery);
        if (!ownerTransform.has_value())
            return false;

        glm::vec3 origin = ownerTransform->position + glm::vec3(0.0f, eyeOffset, 0.0f);

        auto targetPosOpt = resolveTargetPosition(targetKey, blackboard, eyeOffset);
        if (!targetPosOpt.has_value())
            return false;

        glm::vec3 toTarget = *targetPosOpt - origin;
        float distance = glm::length(toTarget);

        if (distance > maxDistance || distance < 0.001f)
            return false;

        glm::vec3 direction = toTarget / distance;

        events::physics::RaycastQuery rayQuery;
        rayQuery.origin = origin;
        rayQuery.direction = direction;
        rayQuery.maxDistance = distance;

        auto hit = dispatcher.query(rayQuery);

        // If nothing was hit, or the hit is beyond the target, line of sight is clear
        return !hit.hit || hit.distance >= distance - 0.1f;
    }

    // === Service hooks (VK-1456) ===

    void BehaviorTreeAdapter::onServiceTick(services::EntityHandle entity, const BTNode& node,
                                            Blackboard& blackboard, float deltaTime)
    {
        (void)deltaTime;
        const std::string serviceType = getServiceProp<std::string>(node, "serviceType", std::string("EQSRefresh"));

        if (serviceType == "EQSRefresh")
        {
            const std::string queryName = getServiceProp<std::string>(node, "queryName", std::string{});
            const std::string resultKey = getServiceProp<std::string>(node, "resultKey", std::string("eqsResult"));
            if (queryName.empty())
                return;

            const std::string key = eqsServiceKey(entity.id, node.id);
            auto it = pendingEQSQueries.find(key);
            if (it != pendingEQSQueries.end())
            {
                eqs::EQSResult result = pollEQS(it->second);
                if (result.status == eqs::EQSQueryStatus::Completed)
                {
                    if (result.hasResults())
                        blackboard.set(resultKey, result.getBestPosition());
                    pendingEQSQueries.erase(it);
                }
                else if (result.status == eqs::EQSQueryStatus::Failed)
                {
                    pendingEQSQueries.erase(it);
                }
                else
                {
                    return; // still in flight — wait for it before resubmitting
                }
            }

            auto handle = submitEQS(entity, queryName);
            if (handle.isValid())
                pendingEQSQueries[key] = handle;
        }
        else if (serviceType == "LineOfSightRefresh")
        {
            const std::string targetKey = getServiceProp<std::string>(node, "targetKey", std::string("target"));
            const std::string visibilityKey = getServiceProp<std::string>(node, "visibilityKey", std::string("targetVisible"));
            const float maxDistance = getServiceProp<float>(node, "maxDistance", 50.0f);
            const float eyeOffset = getServiceProp<float>(node, "eyeOffset", 1.6f);
            const bool visible = computeLineOfSight(entity, targetKey, maxDistance, eyeOffset, blackboard);
            blackboard.set(visibilityKey, visible);
        }
        else if (serviceType == "FocusUpdate")
        {
            const std::string targetKey = getServiceProp<std::string>(node, "targetKey", std::string("target"));
            const std::string focusKey = getServiceProp<std::string>(node, "focusKey", std::string("focusPoint"));
            auto pos = resolveTargetPosition(targetKey, blackboard, 0.0f);
            if (pos.has_value())
                blackboard.set(focusKey, *pos);
        }
    }

    void BehaviorTreeAdapter::onServiceEnd(services::EntityHandle entity, const BTNode& node,
                                           Blackboard& blackboard)
    {
        (void)blackboard;
        // Only EQSRefresh holds in-flight async work to cancel. Sensory result keys are left intact
        // so downstream logic keeps the last known value after the branch deactivates.
        const std::string serviceType = getServiceProp<std::string>(node, "serviceType", std::string("EQSRefresh"));
        if (serviceType != "EQSRefresh")
            return;

        const std::string key = eqsServiceKey(entity.id, node.id);
        auto it = pendingEQSQueries.find(key);
        if (it != pendingEQSQueries.end())
        {
            cancelEQS(it->second);
            pendingEQSQueries.erase(it);
        }
    }
}

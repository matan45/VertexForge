#include "BehaviorTreeAdapter.hpp"
#include "../../../services/providers/scripting/IScriptingProvider.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/navmesh/NavmeshEvents.hpp"
#include "../../../services/events/physics/ControllerEvents.hpp"
#include "../../../services/events/animation/AnimatorEvents.hpp"
#include "print/Log.hpp"

namespace core
{
    using namespace behaviortree;

    BehaviorTreeAdapter::BehaviorTreeAdapter(services::IScriptingProvider* scriptingProvider)
        : scriptingProvider(scriptingProvider)
    {
    }

    BehaviorTreeAdapter::~BehaviorTreeAdapter() = default;

    bool BehaviorTreeAdapter::attachTree(services::EntityHandle entity, const std::string& treePath)
    {
        if (!entity.isValid() || treePath.empty())
        {
            return false;
        }

        // Detach existing tree if any
        detachTree(entity);

        auto dataOpt = BehaviorTreeAsset::load(treePath);
        if (!dataOpt.has_value())
        {
            vfLogError("Failed to load behavior tree: {}", treePath);
            return false;
        }

        RuntimeInstance instance;
        instance.runtime = std::make_unique<BehaviorTreeRuntime>();
        instance.runtime->init(std::move(dataOpt.value()), entity);
        instance.treePath = treePath;
        instance.enabled = true;

        runtimes[entity.id] = std::move(instance);
        vfLogInfo("Attached behavior tree '{}' to entity {}", treePath, entity.id);
        return true;
    }

    void BehaviorTreeAdapter::detachTree(services::EntityHandle entity)
    {
        auto it = runtimes.find(entity.id);
        if (it != runtimes.end())
        {
            // Clean up script instances belonging to this entity's tree
            if (it->second.runtime && scriptingProvider)
            {
                for (const auto& node : it->second.runtime->getTreeData().graph.nodes)
                {
                    if (node.type == BTNodeType::ScriptTask && !node.scriptPath.empty())
                    {
                        ScriptInstanceKey key{entity.id, node.scriptPath};
                        auto sit = scriptInstances.find(key);
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
            }
            runtimes.erase(it);
        }
    }

    void BehaviorTreeAdapter::setEnabled(services::EntityHandle entity, bool enabled)
    {
        auto it = runtimes.find(entity.id);
        if (it != runtimes.end())
        {
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
        if (it != runtimes.end())
        {
            return it->second.treePath;
        }
        return "";
    }

    bool BehaviorTreeAdapter::isEnabled(services::EntityHandle entity) const
    {
        auto it = runtimes.find(entity.id);
        if (it != runtimes.end())
        {
            return it->second.enabled;
        }
        return false;
    }

    // Bug 3 fix: snapshot entity IDs before iterating to avoid iterator invalidation
    // if a script task dispatches events that call detachTree() mid-iteration.
    void BehaviorTreeAdapter::updateAll(float deltaTime)
    {
        std::vector<uint64_t> entityIds;
        entityIds.reserve(runtimes.size());
        for (const auto& [entityId, instance] : runtimes)
        {
            entityIds.push_back(entityId);
        }

        for (uint64_t entityId : entityIds)
        {
            auto it = runtimes.find(entityId);
            if (it == runtimes.end()) continue;

            auto& instance = it->second;
            if (!instance.enabled || !instance.runtime) continue;

            instance.runtime->tick(deltaTime, this);
        }
    }

    void BehaviorTreeAdapter::stopAll()
    {
        for (auto& [entityId, instance] : runtimes)
        {
            if (instance.runtime)
            {
                instance.runtime->reset();
            }
        }
    }

    void BehaviorTreeAdapter::setBlackboardValue(services::EntityHandle entity, const std::string& key,
                                                  const BlackboardValue& value)
    {
        auto it = runtimes.find(entity.id);
        if (it != runtimes.end() && it->second.runtime)
        {
            it->second.runtime->getBlackboard().set(key, value);
        }
    }

    BlackboardValue BehaviorTreeAdapter::getBlackboardValue(services::EntityHandle entity,
                                                             const std::string& key)
    {
        auto it = runtimes.find(entity.id);
        if (it != runtimes.end() && it->second.runtime)
        {
            return it->second.runtime->getBlackboard().get(key);
        }
        return 0.0f;
    }

    bool BehaviorTreeAdapter::hasBlackboardKey(services::EntityHandle entity, const std::string& key) const
    {
        auto it = runtimes.find(entity.id);
        if (it != runtimes.end() && it->second.runtime)
        {
            return it->second.runtime->getBlackboard().has(key);
        }
        return false;
    }

    // === IBTTaskExecutor ===

    // Bug 4 fix: only dispatch SetArrivalDistanceCommand on first tick
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
            // Set arrival distance on the controller (only once)
            events::controller::SetArrivalDistanceCommand arrivalCmd;
            arrivalCmd.entity = entity;
            arrivalCmd.arrivalDistance = arrivalDistance;
            dispatcher.execute(arrivalCmd);

            // Set navmesh agent destination (only once)
            events::navmesh::SetAgentDestinationCommand navCmd;
            navCmd.entity = entity;
            navCmd.target = target;
            dispatcher.execute(navCmd);
        }

        // Check if we've reached the destination
        events::controller::HasReachedDestinationQuery reachedQuery;
        reachedQuery.entity = entity;
        bool reached = dispatcher.query(reachedQuery);

        return reached ? BTNodeStatus::Success : BTNodeStatus::Running;
    }

    BTNodeStatus BehaviorTreeAdapter::executePlayAnimation(services::EntityHandle entity,
                                                            const std::string& stateName,
                                                            bool waitForCompletion)
    {
        if (stateName.empty())
        {
            return BTNodeStatus::Failure;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        // Force transition to the target animation state
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
        {
            return BTNodeStatus::Failure;
        }

        // Bug 2 fix: collision-safe key using pair instead of XOR
        ScriptInstanceKey scriptKey{entity.id, scriptPath};

        // Load script instance if not already loaded
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

        uint64_t instanceId = it->second;

        // Call tick(deltaTime) and map result string to BTNodeStatus
        std::string result = scriptingProvider->callMethodWithReturn(
            instanceId, "tick", {std::any(deltaTime)});

        if (result == "success") return BTNodeStatus::Success;
        if (result == "running") return BTNodeStatus::Running;
        return BTNodeStatus::Failure;
    }

    BTNodeStatus BehaviorTreeAdapter::executeLog(const std::string& message, LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Info:
            vfLogInfo("[BT] {}", message);
            break;
        case LogLevel::Warn:
            vfLogWarning("[BT] {}", message);
            break;
        case LogLevel::Error:
            vfLogError("[BT] {}", message);
            break;
        }

        return BTNodeStatus::Success;
    }
}

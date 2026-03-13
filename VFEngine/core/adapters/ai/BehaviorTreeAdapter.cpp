#include "BehaviorTreeAdapter.hpp"
#include "../../../services/providers/scripting/IScriptingProvider.hpp"
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
        instance.treeData = std::make_unique<BehaviorTreeData>(std::move(dataOpt.value()));
        instance.runtime = std::make_unique<BehaviorTreeRuntime>();
        instance.runtime->init(*instance.treeData, entity);
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

    void BehaviorTreeAdapter::updateAll(float deltaTime)
    {
        for (auto& [entityId, instance] : runtimes)
        {
            if (!instance.enabled || !instance.runtime)
            {
                continue;
            }

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

    // === IBTTaskExecutor ===

    BTNodeStatus BehaviorTreeAdapter::executeMoveTo(services::EntityHandle entity,
                                                     const std::string& targetKey,
                                                     float arrivalDistance,
                                                     Blackboard& blackboard)
    {
        if (!blackboard.has(targetKey))
        {
            vfLogWarning("BT MoveTo: blackboard key '{}' not found", targetKey);
            return BTNodeStatus::Failure;
        }

        // TODO: Dispatch NavMesh/Controller CQRS events when those systems are wired
        // For now, return Success as placeholder
        // glm::vec3 target = blackboard.getVec3(targetKey);
        // events::EventDispatcher::instance().execute(events::navmesh::SetAgentDestinationCommand{entity, target});
        // auto reached = events::EventDispatcher::instance().query(events::controller::HasReachedDestinationQuery{entity});
        // return reached ? BTNodeStatus::Success : BTNodeStatus::Running;

        return BTNodeStatus::Success;
    }

    BTNodeStatus BehaviorTreeAdapter::executePlayAnimation(services::EntityHandle entity,
                                                            const std::string& stateName,
                                                            bool waitForCompletion)
    {
        if (stateName.empty())
        {
            return BTNodeStatus::Failure;
        }

        // TODO: Dispatch Animator CQRS events when wired
        // events::EventDispatcher::instance().execute(
        //     events::animator::ForceEntityTransitionToCommand{entity, stateName});
        // if (waitForCompletion) {
        //     auto time = events::EventDispatcher::instance().query(
        //         events::animator::GetEntityAnimatorNormalizedTimeQuery{entity});
        //     return time >= 1.0f ? BTNodeStatus::Success : BTNodeStatus::Running;
        // }

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

        // TODO: Load script instance and call tick method via ScriptingProvider
        // auto instanceInfo = scriptingProvider->loadScript(scriptPath, entity);
        // if (!instanceInfo) return BTNodeStatus::Failure;
        // auto result = scriptingProvider->callMethod(instanceInfo->instanceId, "tick", {deltaTime});
        // Map result string to BTNodeStatus

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

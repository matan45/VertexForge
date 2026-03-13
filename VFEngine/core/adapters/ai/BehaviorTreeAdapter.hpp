#pragma once
#include "../../../services/providers/ai/IBehaviorTreeProvider.hpp"
#include "../../../utilities/behaviortree/BehaviorTreeRuntime.hpp"
#include "../../../utilities/behaviortree/BehaviorTreeAsset.hpp"
#include <unordered_map>
#include <memory>

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

        void updateAll(float deltaTime) override;
        void stopAll() override;

        void setBlackboardValue(services::EntityHandle entity, const std::string& key,
                                const behaviortree::BlackboardValue& value) override;
        behaviortree::BlackboardValue getBlackboardValue(services::EntityHandle entity,
                                                          const std::string& key) override;

        // === IBTTaskExecutor ===
        behaviortree::BTNodeStatus executeMoveTo(services::EntityHandle entity,
                                                  const std::string& targetKey,
                                                  float arrivalDistance,
                                                  behaviortree::Blackboard& blackboard) override;

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

    private:
        struct RuntimeInstance
        {
            std::unique_ptr<behaviortree::BehaviorTreeData> treeData;
            std::unique_ptr<behaviortree::BehaviorTreeRuntime> runtime;
            std::string treePath;
            bool enabled = true;
        };

        services::IScriptingProvider* scriptingProvider;
        std::unordered_map<uint64_t, RuntimeInstance> runtimes; // keyed by EntityHandle::id
    };
}

#pragma once
#include "../../data/EntityHandle.hpp"
#include "../../../utilities/behaviortree/BehaviorTreeTypes.hpp"
#include <string>

namespace services {

    class IBehaviorTreeProvider {
    public:
        virtual ~IBehaviorTreeProvider() = default;

        // === Tree Management ===
        virtual bool attachTree(EntityHandle entity, const std::string& treePath) = 0;
        virtual void detachTree(EntityHandle entity) = 0;
        virtual void setEnabled(EntityHandle entity, bool enabled) = 0;
        virtual bool hasTree(EntityHandle entity) const = 0;
        virtual std::string getTreePath(EntityHandle entity) const = 0;
        virtual bool isEnabled(EntityHandle entity) const = 0;

        // === Runtime ===
        virtual void updateAll(float deltaTime) = 0;
        virtual void stopAll() = 0;

        // === Blackboard ===
        virtual void setBlackboardValue(EntityHandle entity, const std::string& key,
                                        const behaviortree::BlackboardValue& value) = 0;
        virtual behaviortree::BlackboardValue getBlackboardValue(EntityHandle entity,
                                                                  const std::string& key) = 0;
        virtual bool hasBlackboardKey(EntityHandle entity, const std::string& key) const = 0;
    };

}

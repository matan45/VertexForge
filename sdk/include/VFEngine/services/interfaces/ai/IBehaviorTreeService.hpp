#pragma once
#include "../../data/EntityHandle.hpp"
#include "../../../utilities/behaviortree/BehaviorTreeTypes.hpp"
#include <string>

namespace services
{
    class IBehaviorTreeService
    {
    public:
        virtual ~IBehaviorTreeService() = default;

        virtual void registerEventHandlers() = 0;

        // === Tree Management ===
        virtual bool attachTree(EntityHandle entity, const std::string& treePath) = 0;
        virtual void detachTree(EntityHandle entity) = 0;
        virtual void setEnabled(EntityHandle entity, bool enabled) = 0;

        // === Runtime ===
        virtual void updateAll(float deltaTime) = 0;
        virtual void stopAll() = 0;
    };
}

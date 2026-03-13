#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../../utilities/behaviortree/BehaviorTreeTypes.hpp"
#include <string>
#include <optional>

namespace events::ai {

    struct AttachBehaviorTreeCommand : ICommand<bool> {
        services::EntityHandle entity;
        std::string treePath;

        std::string_view getName() const override { return "AttachBehaviorTree"; }
    };

    struct DetachBehaviorTreeCommand : ICommand<> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "DetachBehaviorTree"; }
    };

    struct SetBehaviorTreeEnabledCommand : ICommand<> {
        services::EntityHandle entity;
        bool enabled;

        std::string_view getName() const override { return "SetBehaviorTreeEnabled"; }
    };

    struct SetBlackboardValueCommand : ICommand<> {
        services::EntityHandle entity;
        std::string key;
        behaviortree::BlackboardValue value;

        std::string_view getName() const override { return "SetBlackboardValue"; }
    };

    struct GetBlackboardValueQuery : IQuery<behaviortree::BlackboardValue> {
        services::EntityHandle entity;
        std::string key;

        std::string_view getName() const override { return "GetBlackboardValue"; }
    };

    struct HasBehaviorTreeQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasBehaviorTree"; }
    };

    struct GetBehaviorTreePathQuery : IQuery<std::string> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetBehaviorTreePath"; }
    };

    struct IsBehaviorTreeEnabledQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "IsBehaviorTreeEnabled"; }
    };

}

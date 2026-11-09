#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/BehaviorTreeComponentData.hpp"
#include <optional>
#include <string_view>

// VK-1660: whole-component data access to an entity's BehaviorTreeComponent (tree, enabled flag and
// per-entity blackboard start values). Handled by services::BehaviorTreeServiceImpl. MAIN THREAD ONLY.
namespace events::ai {

    // nullopt when the entity is invalid or has no BehaviorTreeComponent.
    struct GetBehaviorTreeComponentDataQuery : IQuery<std::optional<services::BehaviorTreeComponentData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetBehaviorTreeComponentData"; }
    };

    // Replaces the component's tree, enabled flag and initial values. Returns false when the entity is
    // invalid or has no BehaviorTreeComponent, or a non-empty treePath is not an existing .vfBehaviorTree
    // (AssetRef::fromPath would register the missing file). Takes effect at the next Edit->Play: live
    // runtimes are not touched (SetBlackboardValueCommand writes those).
    struct SetBehaviorTreeComponentDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::BehaviorTreeComponentData data;

        std::string_view getName() const override { return "SetBehaviorTreeComponentData"; }
    };

}

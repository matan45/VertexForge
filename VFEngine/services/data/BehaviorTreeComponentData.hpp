#pragma once
#include "EntityHandle.hpp"
#include <behaviortree/BehaviorTreeTypes.hpp>
#include <behaviortree/BlackboardInitialValue.hpp>
#include <string>
#include <vector>

// VK-1660: an entity's BehaviorTreeComponent as the Services layer exposes it (events in
// services/events/ai/BehaviorTreeComponentEvents.hpp). Header-only value types.
namespace services
{
    // One per-entity blackboard start value, applied after the tree attaches at Edit->Play.
    struct BehaviorTreeInitialValueData
    {
        std::string key;
        // Entity values are live handles: what the stored reference resolves to now (UUID first, then
        // the first entity with the stored name), invalid when it resolves to nothing.
        behaviortree::BlackboardValue value = 0.0f;
        // Entity values only. Get: the stored reference. Set: used when `value` is an invalid handle, so
        // a Get -> Set round trip (undo) keeps a reference that resolves to nothing; a valid handle is
        // stored as that entity's {UUID, name} instead.
        behaviortree::BlackboardEntityRef entityRef;
        // Get only: an Entity value whose stored (non-empty) reference resolves to no entity.
        bool unresolved = false;
    };

    struct BehaviorTreeComponentData
    {
        // Get: the tree asset's resolved path. Set: any path AssetRef::fromPath accepts ("" = no tree).
        std::string treePath;
        bool enabled = true;
        std::vector<BehaviorTreeInitialValueData> initialValues;
    };
}

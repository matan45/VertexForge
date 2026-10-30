#pragma once

#include "../../data/BehaviorTreeComponentData.hpp"
#include "../../data/EntityHandle.hpp"
#include <behaviortree/BehaviorTreeTypes.hpp>
#include <behaviortree/BlackboardInitialValue.hpp>
#include <entt/entt.hpp>
#include <optional>

namespace components
{
    struct BehaviorTreeComponent;
}

// VK-1660: per-entity blackboard start values (BehaviorTreeComponent::initialValues). Entity values are
// stored as a {UUID, name} reference and resolved against the live registry when they are read.
namespace services
{
    // {UUIDComponent value, NameComponent name} of `entity`; 0 / "" for a missing component.
    behaviortree::BlackboardEntityRef makeBlackboardEntityRef(const entt::registry& registry, entt::entity entity);

    // The live entity `ref` points at: the UUID first (EntityRegistry::findByUUID; the entity must still
    // carry that UUID), else the entity named exactly ref.name with the lowest entity index (so duplicate
    // names resolve deterministically). Invalid when neither matches or `ref` is empty.
    EntityHandle resolveBlackboardEntityRef(const entt::registry& registry, const behaviortree::BlackboardEntityRef& ref);

    // The blackboard type of a stored start value (BlackboardInitialData shares BlackboardValue's order).
    behaviortree::BlackboardValueType initialValueType(const behaviortree::BlackboardInitialData& value);

    // The value a runtime blackboard starts with: an Entity reference becomes the handle it resolves to
    // (an empty reference = no entity). nullopt for a non-empty reference that resolves to nothing.
    std::optional<behaviortree::BlackboardValue> resolveInitialValue(const entt::registry& registry,
                                                                     const behaviortree::BlackboardInitialData& value);

    // Stored value -> DTO: Entity values resolved, with the stored reference and the unresolved flag.
    BehaviorTreeInitialValueData makeInitialValueData(const entt::registry& registry,
                                                      const behaviortree::BlackboardInitialValue& stored);

    // DTO -> stored value: a live Entity handle becomes that entity's {UUID, name}; an invalid (or dead)
    // handle keeps the DTO's entityRef, so a Get -> Set round trip preserves an unresolved reference.
    behaviortree::BlackboardInitialValue makeStoredInitialValue(const entt::registry& registry,
                                                                const BehaviorTreeInitialValueData& data);

    // GetBehaviorTreeComponentDataQuery's reply for `component`.
    BehaviorTreeComponentData makeBehaviorTreeComponentData(const entt::registry& registry,
                                                            const components::BehaviorTreeComponent& component);
}

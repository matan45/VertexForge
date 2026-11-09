#include "BehaviorTreeInitialValues.hpp"
#include "../../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"

#include <type_traits>
#include <variant>

namespace services
{
    namespace bt = behaviortree;

    // The conversions below rely on BlackboardValue, BlackboardInitialData and BlackboardValueType
    // listing their alternatives in the same order (BlackboardInitialValue.hpp asserts its own order).
    static_assert(std::variant_size_v<bt::BlackboardValue> == std::variant_size_v<bt::BlackboardInitialData>);
    static_assert(static_cast<size_t>(bt::BlackboardValueType::Float) == 0);
    static_assert(static_cast<size_t>(bt::BlackboardValueType::Int) == 1);
    static_assert(static_cast<size_t>(bt::BlackboardValueType::Bool) == 2);
    static_assert(static_cast<size_t>(bt::BlackboardValueType::String) == 3);
    static_assert(static_cast<size_t>(bt::BlackboardValueType::Vec3) == 4);
    static_assert(static_cast<size_t>(bt::BlackboardValueType::Entity) == 5);
    static_assert(std::is_same_v<std::variant_alternative_t<5, bt::BlackboardValue>, EntityHandle>);

    bt::BlackboardEntityRef makeBlackboardEntityRef(const entt::registry& registry, entt::entity entity)
    {
        bt::BlackboardEntityRef ref;
        if (!registry.valid(entity))
            return ref;
        if (const auto* uuid = registry.try_get<components::UUIDComponent>(entity))
            ref.uuid = uuid->id.getValue();
        if (const auto* name = registry.try_get<components::NameComponent>(entity))
            ref.name = name->name;
        return ref;
    }

    EntityHandle resolveBlackboardEntityRef(const entt::registry& registry, const bt::BlackboardEntityRef& ref)
    {
        if (ref.uuid != 0)
        {
            const entt::entity found = scene::EntityRegistry::findByUUID(ref.uuid);
            if (found != entt::null && registry.valid(found))
            {
                const auto* uuid = registry.try_get<components::UUIDComponent>(found);
                if (uuid && uuid->id.getValue() == ref.uuid)
                    return internal::toHandle(found);
            }
        }

        if (!ref.name.empty())
        {
            entt::entity best = entt::null;
            for (const auto [entity, name] : registry.view<components::NameComponent>().each())
            {
                if (name.name != ref.name)
                    continue;
                if (best == entt::null || entt::to_entity(entity) < entt::to_entity(best))
                    best = entity;
            }
            if (best != entt::null)
                return internal::toHandle(best);
        }
        return EntityHandle::invalid();
    }

    bt::BlackboardValueType initialValueType(const bt::BlackboardInitialData& value)
    {
        return static_cast<bt::BlackboardValueType>(value.index());
    }

    std::optional<bt::BlackboardValue> resolveInitialValue(const entt::registry& registry,
                                                          const bt::BlackboardInitialData& value)
    {
        return std::visit([&registry](const auto& stored) -> std::optional<bt::BlackboardValue>
        {
            using T = std::decay_t<decltype(stored)>;
            if constexpr (std::is_same_v<T, bt::BlackboardEntityRef>)
            {
                if (stored.empty())
                    return bt::BlackboardValue{EntityHandle::invalid()};
                const EntityHandle handle = resolveBlackboardEntityRef(registry, stored);
                if (!handle.isValid())
                    return std::nullopt;
                return bt::BlackboardValue{handle};
            }
            else
            {
                return bt::BlackboardValue{stored};
            }
        }, value);
    }

    BehaviorTreeInitialValueData makeInitialValueData(const entt::registry& registry,
                                                      const bt::BlackboardInitialValue& stored)
    {
        BehaviorTreeInitialValueData data;
        data.key = stored.key;
        if (const auto* ref = std::get_if<bt::BlackboardEntityRef>(&stored.value))
        {
            const EntityHandle handle = resolveBlackboardEntityRef(registry, *ref);
            data.value = handle;
            data.entityRef = *ref;
            data.unresolved = !ref->empty() && !handle.isValid();
            return data;
        }
        data.value = *resolveInitialValue(registry, stored.value); // non-Entity values always convert
        return data;
    }

    bt::BlackboardInitialValue makeStoredInitialValue(const entt::registry& registry,
                                                      const BehaviorTreeInitialValueData& data)
    {
        bt::BlackboardInitialValue stored;
        stored.key = data.key;
        stored.value = std::visit([&registry, &data](const auto& value) -> bt::BlackboardInitialData
        {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, EntityHandle>)
            {
                if (value.isValid() && registry.valid(internal::fromHandle(value)))
                    return makeBlackboardEntityRef(registry, internal::fromHandle(value));
                return data.entityRef;
            }
            else
            {
                return value;
            }
        }, data.value);
        return stored;
    }

    BehaviorTreeComponentData makeBehaviorTreeComponentData(const entt::registry& registry,
                                                            const components::BehaviorTreeComponent& component)
    {
        BehaviorTreeComponentData data;
        if (component.behaviorTreeRef.isValid())
            data.treePath = component.behaviorTreeRef.resolve();
        data.enabled = component.enabled;
        data.initialValues.reserve(component.initialValues.size());
        for (const auto& stored : component.initialValues)
            data.initialValues.push_back(makeInitialValueData(registry, stored));
        return data;
    }
}

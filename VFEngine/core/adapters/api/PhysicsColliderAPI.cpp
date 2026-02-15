// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "PhysicsColliderAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"

namespace core::api
{
    namespace
    {
        template<typename Accessor>
        value::Value getColliderProperty(const std::vector<value::Value>& args,
                                         Accessor&& accessor, const value::Value& defaultVal)
        {
            if (args.empty()) return defaultVal;
            auto entity = resolveEntity(args[0]);
            if (!entity) return defaultVal;
            auto& registry = scene::EntityRegistry::getRegistry();
            if (!registry.all_of<components::ColliderComponent>(*entity))
                return defaultVal;
            return value::Value(accessor(registry.get<components::ColliderComponent>(*entity)));
        }

        template<typename Mutator>
        value::Value setColliderProperty(const std::vector<value::Value>& args, Mutator&& mutator)
        {
            if (args.size() < 2) return value::Value(std::monostate{});
            auto entity = resolveEntity(args[0]);
            if (!entity) return value::Value(std::monostate{});
            auto& registry = scene::EntityRegistry::getRegistry();
            if (!registry.all_of<components::ColliderComponent>(*entity))
                return value::Value(std::monostate{});
            mutator(registry.get<components::ColliderComponent>(*entity), args);
            return value::Value(std::monostate{});
        }

        void registerColliderQueryFunctions(services::ScriptInterpreter* interpreter)
        {
            interpreter->registerNativeFunction("_native_physics_hasCollider",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.empty()) return value::Value(false);
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(false);
                    auto& registry = scene::EntityRegistry::getRegistry();
                    return value::Value(registry.all_of<components::ColliderComponent>(*entity));
                });

            interpreter->registerNativeFunction("_native_physics_getColliderShape",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    return getColliderProperty(args,
                        [](const components::ColliderComponent& c) { return static_cast<int64_t>(c.shape); },
                        value::Value(static_cast<int64_t>(0)));
                });

            interpreter->registerNativeFunction("_native_physics_getColliderSize",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.empty()) return makeVec3Array(glm::vec3(1.0f));
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return makeVec3Array(glm::vec3(1.0f));
                    auto& registry = scene::EntityRegistry::getRegistry();
                    if (!registry.all_of<components::ColliderComponent>(*entity))
                        return makeVec3Array(glm::vec3(1.0f));
                    return makeVec3Array(registry.get<components::ColliderComponent>(*entity).size);
                });

            interpreter->registerNativeFunction("_native_physics_getColliderHeight",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    return getColliderProperty(args,
                        [](const components::ColliderComponent& c) { return c.height; },
                        value::Value(2.0f));
                });

            interpreter->registerNativeFunction("_native_physics_getColliderOffset",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.empty()) return makeVec3Array(glm::vec3(0.0f));
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return makeVec3Array(glm::vec3(0.0f));
                    auto& registry = scene::EntityRegistry::getRegistry();
                    if (!registry.all_of<components::ColliderComponent>(*entity))
                        return makeVec3Array(glm::vec3(0.0f));
                    return makeVec3Array(registry.get<components::ColliderComponent>(*entity).offset);
                });

            interpreter->registerNativeFunction("_native_physics_isTrigger",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    return getColliderProperty(args,
                        [](const components::ColliderComponent& c) { return c.isTrigger; },
                        value::Value(false));
                });

            interpreter->registerNativeFunction("_native_physics_getCollisionLayer",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    return getColliderProperty(args,
                        [](const components::ColliderComponent& c) { return static_cast<int64_t>(c.collisionLayer); },
                        value::Value(static_cast<int64_t>(1)));
                });

            interpreter->registerNativeFunction("_native_physics_getFriction",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    return getColliderProperty(args,
                        [](const components::ColliderComponent& c) { return c.friction; },
                        value::Value(0.5f));
                });

            interpreter->registerNativeFunction("_native_physics_getRestitution",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    return getColliderProperty(args,
                        [](const components::ColliderComponent& c) { return c.restitution; },
                        value::Value(0.0f));
                });
        }

        void registerColliderSetterFunctions(services::ScriptInterpreter* interpreter)
        {
            interpreter->registerNativeFunction("_native_physics_setColliderSize",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.size() < 4) return value::Value(std::monostate{});
                    return setColliderProperty(args, [](components::ColliderComponent& c, auto& a)
                    {
                        c.size = glm::vec3(extractFloat(a[1]), extractFloat(a[2]), extractFloat(a[3]));
                    });
                });

            interpreter->registerNativeFunction("_native_physics_setColliderHeight",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    return setColliderProperty(args, [](components::ColliderComponent& c, auto& a)
                    {
                        c.height = extractFloat(a[1]);
                    });
                });

            interpreter->registerNativeFunction("_native_physics_setTrigger",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    return setColliderProperty(args, [](components::ColliderComponent& c, auto& a)
                    {
                        c.isTrigger = extractBool(a[1]);
                    });
                });

            interpreter->registerNativeFunction("_native_physics_setCollisionLayer",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.size() < 2) return value::Value(std::monostate{});
                    int64_t layer = extractInt64(args[1]);
                    if (layer < 0 || layer > 15) return value::Value(std::monostate{});
                    return setColliderProperty(args, [layer](components::ColliderComponent& c, auto&)
                    {
                        c.collisionLayer = static_cast<uint8_t>(layer);
                    });
                });

            interpreter->registerNativeFunction("_native_physics_setFriction",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    return setColliderProperty(args, [](components::ColliderComponent& c, auto& a)
                    {
                        c.friction = extractFloat(a[1]);
                    });
                });

            interpreter->registerNativeFunction("_native_physics_setRestitution",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    return setColliderProperty(args, [](components::ColliderComponent& c, auto& a)
                    {
                        c.restitution = extractFloat(a[1]);
                    });
                });
        }
    }

    void PhysicsColliderAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        registerColliderQueryFunctions(interpreter);
        registerColliderSetterFunctions(interpreter);
    }
}

// mType headers must come first to avoid Windows macro conflicts
#include "print/Log.hpp"
#include <services/ScriptInterpreter.hpp>

#include "PhysicsRigidBodyAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/PhysicsEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"

namespace core::api
{
    static int forceApplicationCountThisFrame = 0;

    namespace
    {
        template<typename Accessor>
        value::Value getRBProperty(const std::vector<value::Value>& args,
                                   Accessor&& accessor, const value::Value& defaultVal)
        {
            if (args.empty()) return defaultVal;
            auto entity = resolveEntity(args[0]);
            if (!entity) return defaultVal;
            auto& registry = scene::EntityRegistry::getRegistry();
            if (!registry.all_of<components::RigidBodyComponent>(*entity))
                return defaultVal;
            return value::Value(accessor(registry.get<components::RigidBodyComponent>(*entity)));
        }

        template<typename Mutator>
        value::Value setRBProperty(const std::vector<value::Value>& args, Mutator&& mutator)
        {
            if (args.size() < 2) return value::Value(std::monostate{});
            auto entity = resolveEntity(args[0]);
            if (!entity) return value::Value(std::monostate{});
            auto& registry = scene::EntityRegistry::getRegistry();
            if (!registry.all_of<components::RigidBodyComponent>(*entity))
                return value::Value(std::monostate{});
            mutator(registry.get<components::RigidBodyComponent>(*entity), args);
            return value::Value(std::monostate{});
        }

        bool checkForceRateLimit(int& counter, int max, const char* context)
        {
            if (counter >= max)
            {
                vfLogWarning("[Script] {} rate limit exceeded ({}/frame)", context, max);
                return false;
            }
            counter++;
            return true;
        }

        glm::vec3 clampMagnitude(glm::vec3 v, float maxMag)
        {
            float mag = glm::length(v);
            if (mag > maxMag)
            {
                return (v / mag) * maxMag;
            }
            return v;
        }

        void registerQueryFunctions(services::ScriptInterpreter* interpreter,
                                    events::EventDispatcher& dispatcher)
        {
            interpreter->registerNativeFunction("_native_physics_hasRigidBody",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.empty()) return value::Value(false);
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(false);
                    auto& registry = scene::EntityRegistry::getRegistry();
                    return value::Value(registry.all_of<components::RigidBodyComponent>(*entity));
                });

            interpreter->registerNativeFunction("_native_physics_getBodyType",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    return getRBProperty(args,
                        [](const components::RigidBodyComponent& rb) { return static_cast<int64_t>(rb.type); },
                        value::Value(static_cast<int64_t>(1)));
                });

            interpreter->registerNativeFunction("_native_physics_getMass",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    return getRBProperty(args,
                        [](const components::RigidBodyComponent& rb) { return rb.mass; },
                        value::Value(1.0f));
                });

            interpreter->registerNativeFunction("_native_physics_getLinearDamping",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    return getRBProperty(args,
                        [](const components::RigidBodyComponent& rb) { return rb.linearDamping; },
                        value::Value(0.0f));
                });

            interpreter->registerNativeFunction("_native_physics_getAngularDamping",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    return getRBProperty(args,
                        [](const components::RigidBodyComponent& rb) { return rb.angularDamping; },
                        value::Value(0.05f));
                });

            interpreter->registerNativeFunction("_native_physics_getLinearVelocity",
                [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.empty()) return makeVec3Array(glm::vec3(0.0f));
                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return makeVec3Array(glm::vec3(0.0f));

                    events::physics::GetLinearVelocityQuery query;
                    query.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                    return makeVec3Array(dispatcher.query(query));
                });

            interpreter->registerNativeFunction("_native_physics_getAngularVelocity",
                [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.empty()) return makeVec3Array(glm::vec3(0.0f));
                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return makeVec3Array(glm::vec3(0.0f));

                    events::physics::GetAngularVelocityQuery query;
                    query.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                    return makeVec3Array(dispatcher.query(query));
                });

            interpreter->registerNativeFunction("_native_physics_getPosition",
                [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.empty()) return makeVec3Array(glm::vec3(0.0f));
                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return makeVec3Array(glm::vec3(0.0f));

                    events::physics::GetPhysicsPositionQuery query;
                    query.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                    return makeVec3Array(dispatcher.query(query));
                });

            interpreter->registerNativeFunction("_native_physics_getRotation",
                [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                {
                    auto defaultQuat = std::make_shared<value::NativeArray>(4, value::ValueType::FLOAT);
                    defaultQuat->set(0, value::Value(0.0f));
                    defaultQuat->set(1, value::Value(0.0f));
                    defaultQuat->set(2, value::Value(0.0f));
                    defaultQuat->set(3, value::Value(1.0f));

                    if (args.empty()) return value::Value(defaultQuat);
                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return value::Value(defaultQuat);

                    events::physics::GetPhysicsRotationQuery query;
                    query.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                    glm::quat rot = dispatcher.query(query);

                    auto result = std::make_shared<value::NativeArray>(4, value::ValueType::FLOAT);
                    result->set(0, value::Value(rot.x));
                    result->set(1, value::Value(rot.y));
                    result->set(2, value::Value(rot.z));
                    result->set(3, value::Value(rot.w));
                    return value::Value(result);
                });
        }

        void registerSetterFunctions(services::ScriptInterpreter* interpreter,
                                     events::EventDispatcher& dispatcher)
        {
            interpreter->registerNativeFunction("_native_physics_setBodyType",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.size() < 2) return value::Value(std::monostate{});
                    int64_t type = extractInt64(args[1]);
                    if (type < 0 || type > 2) return value::Value(std::monostate{});
                    return setRBProperty(args, [type](components::RigidBodyComponent& rb, auto&)
                    {
                        rb.type = static_cast<components::RigidBodyType>(type);
                    });
                });

            interpreter->registerNativeFunction("_native_physics_setMass",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.size() < 2) return value::Value(std::monostate{});
                    float mass = extractFloat(args[1]);
                    if (mass < 0.0f) return value::Value(std::monostate{});
                    return setRBProperty(args, [mass](components::RigidBodyComponent& rb, auto&)
                    {
                        rb.mass = mass;
                    });
                });

            interpreter->registerNativeFunction("_native_physics_setLinearDamping",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    return setRBProperty(args, [](components::RigidBodyComponent& rb, auto& a)
                    {
                        rb.linearDamping = extractFloat(a[1]);
                    });
                });

            interpreter->registerNativeFunction("_native_physics_setAngularDamping",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    return setRBProperty(args, [](components::RigidBodyComponent& rb, auto& a)
                    {
                        rb.angularDamping = extractFloat(a[1]);
                    });
                });

            interpreter->registerNativeFunction("_native_physics_setLinearVelocity",
                [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.size() < 4) return value::Value(std::monostate{});
                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return value::Value(std::monostate{});

                    events::physics::SetLinearVelocityCommand cmd;
                    cmd.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                    cmd.velocity = glm::vec3(extractFloat(args[1]), extractFloat(args[2]),
                                             extractFloat(args[3]));
                    dispatcher.execute(cmd);
                    return value::Value(std::monostate{});
                });

            interpreter->registerNativeFunction("_native_physics_setAngularVelocity",
                [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.size() < 4) return value::Value(std::monostate{});
                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return value::Value(std::monostate{});

                    events::physics::SetAngularVelocityCommand cmd;
                    cmd.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                    cmd.velocity = glm::vec3(extractFloat(args[1]), extractFloat(args[2]),
                                             extractFloat(args[3]));
                    dispatcher.execute(cmd);
                    return value::Value(std::monostate{});
                });

            interpreter->registerNativeFunction("_native_physics_setPosition",
                [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.size() < 4) return value::Value(std::monostate{});
                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return value::Value(std::monostate{});

                    events::physics::SetPhysicsPositionCommand cmd;
                    cmd.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                    cmd.position = glm::vec3(extractFloat(args[1]), extractFloat(args[2]),
                                             extractFloat(args[3]));
                    dispatcher.execute(cmd);
                    return value::Value(std::monostate{});
                });

            interpreter->registerNativeFunction("_native_physics_setRotation",
                [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.size() < 5) return value::Value(std::monostate{});
                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return value::Value(std::monostate{});

                    events::physics::SetPhysicsRotationCommand cmd;
                    cmd.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                    cmd.rotation = glm::quat(extractFloat(args[4]), extractFloat(args[1]),
                                             extractFloat(args[2]), extractFloat(args[3]));
                    dispatcher.execute(cmd);
                    return value::Value(std::monostate{});
                });
        }

        void registerForceFunctions(services::ScriptInterpreter* interpreter,
                                    events::EventDispatcher& dispatcher)
        {
            interpreter->registerNativeFunction("_native_physics_applyForce",
                [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.size() < 4) return value::Value(std::monostate{});
                    if (!checkForceRateLimit(forceApplicationCountThisFrame,
                        PhysicsRigidBodyAPI::MAX_FORCE_APPLICATIONS_PER_FRAME, "Force application"))
                        return value::Value(std::monostate{});

                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return value::Value(std::monostate{});

                    events::physics::ApplyForceCommand cmd;
                    cmd.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                    cmd.force = clampMagnitude(
                        glm::vec3(extractFloat(args[1]), extractFloat(args[2]), extractFloat(args[3])),
                        PhysicsRigidBodyAPI::MAX_FORCE_MAGNITUDE);
                    dispatcher.execute(cmd);
                    return value::Value(std::monostate{});
                });

            interpreter->registerNativeFunction("_native_physics_applyForceAtPosition",
                [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.size() < 7) return value::Value(std::monostate{});
                    if (!checkForceRateLimit(forceApplicationCountThisFrame,
                        PhysicsRigidBodyAPI::MAX_FORCE_APPLICATIONS_PER_FRAME, "Force application"))
                        return value::Value(std::monostate{});

                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return value::Value(std::monostate{});

                    events::physics::ApplyForceAtPositionCommand cmd;
                    cmd.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                    cmd.force = clampMagnitude(
                        glm::vec3(extractFloat(args[1]), extractFloat(args[2]), extractFloat(args[3])),
                        PhysicsRigidBodyAPI::MAX_FORCE_MAGNITUDE);
                    cmd.position = glm::vec3(extractFloat(args[4]), extractFloat(args[5]),
                                             extractFloat(args[6]));
                    dispatcher.execute(cmd);
                    return value::Value(std::monostate{});
                });

            interpreter->registerNativeFunction("_native_physics_applyImpulse",
                [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.size() < 4) return value::Value(std::monostate{});
                    if (!checkForceRateLimit(forceApplicationCountThisFrame,
                        PhysicsRigidBodyAPI::MAX_FORCE_APPLICATIONS_PER_FRAME, "Force/impulse application"))
                        return value::Value(std::monostate{});

                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return value::Value(std::monostate{});

                    events::physics::ApplyImpulseCommand cmd;
                    cmd.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                    cmd.impulse = clampMagnitude(
                        glm::vec3(extractFloat(args[1]), extractFloat(args[2]), extractFloat(args[3])),
                        PhysicsRigidBodyAPI::MAX_IMPULSE_MAGNITUDE);
                    dispatcher.execute(cmd);
                    return value::Value(std::monostate{});
                });

            interpreter->registerNativeFunction("_native_physics_applyTorque",
                [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.size() < 4) return value::Value(std::monostate{});
                    if (!checkForceRateLimit(forceApplicationCountThisFrame,
                        PhysicsRigidBodyAPI::MAX_FORCE_APPLICATIONS_PER_FRAME, "Torque application"))
                        return value::Value(std::monostate{});

                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return value::Value(std::monostate{});

                    events::physics::ApplyTorqueCommand cmd;
                    cmd.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                    cmd.torque = clampMagnitude(
                        glm::vec3(extractFloat(args[1]), extractFloat(args[2]), extractFloat(args[3])),
                        PhysicsRigidBodyAPI::MAX_TORQUE_MAGNITUDE);
                    dispatcher.execute(cmd);
                    return value::Value(std::monostate{});
                });
        }
    }

    void PhysicsRigidBodyAPI::beginFrame()
    {
        forceApplicationCountThisFrame = 0;
    }

    void PhysicsRigidBodyAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        registerQueryFunctions(interpreter, dispatcher);
        registerSetterFunctions(interpreter, dispatcher);
        registerForceFunctions(interpreter, dispatcher);
    }
}

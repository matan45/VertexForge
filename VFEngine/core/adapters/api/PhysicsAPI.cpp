// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "PhysicsAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/PhysicsEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../../services/data/EntityConversion.hpp"

namespace core::api
{
    void PhysicsAPI::beginFrame()
    {
        raycastCountThisFrame = 0;
        forceApplicationCountThisFrame = 0;
    }

    void PhysicsAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Helper to clamp vector magnitude
        auto clampMagnitude = [](glm::vec3 v, float maxMag) -> glm::vec3
        {
            float mag = glm::length(v);
            if (mag > maxMag)
            {
                return (v / mag) * maxMag;
            }
            return v;
        };

        // ============================================
        // RigidBody Queries
        // ============================================

        // _native_physics_hasRigidBody(entityId) -> bool
        interpreter->registerNativeFunction("_native_physics_hasRigidBody",
                                            [](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.empty())
                                                {
                                                    return value::Value(false);
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                if (id < 0)
                                                {
                                                    return value::Value(false);
                                                }

                                                auto& registry = scene::EntityRegistry::getRegistry();
                                                auto entity = services::internal::fromHandle(services::EntityHandle{
                                                    static_cast<uint64_t>(id)
                                                });
                                                if (!registry.valid(entity))
                                                {
                                                    return value::Value(false);
                                                }

                                                return value::Value(
                                                    registry.all_of<components::RigidBodyComponent>(entity));
                                            });

        // _native_physics_getBodyType(entityId) -> int (0=Static, 1=Dynamic, 2=Kinematic)
        interpreter->registerNativeFunction("_native_physics_getBodyType",
                                            [](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.empty())
                                                {
                                                    return value::Value(static_cast<int64_t>(1)); // Dynamic default
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                if (id < 0)
                                                {
                                                    return value::Value(static_cast<int64_t>(1));
                                                }

                                                auto& registry = scene::EntityRegistry::getRegistry();
                                                auto entity = services::internal::fromHandle(services::EntityHandle{
                                                    static_cast<uint64_t>(id)
                                                });
                                                if (!registry.valid(entity) || !registry.all_of<
                                                    components::RigidBodyComponent>(entity))
                                                {
                                                    return value::Value(static_cast<int64_t>(1));
                                                }

                                                auto& rb = registry.get<components::RigidBodyComponent>(entity);
                                                return value::Value(static_cast<int64_t>(rb.type));
                                            });

        // _native_physics_getMass(entityId) -> float
        interpreter->registerNativeFunction("_native_physics_getMass",
                                            [](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.empty())
                                                {
                                                    return value::Value(1.0f);
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                if (id < 0)
                                                {
                                                    return value::Value(1.0f);
                                                }

                                                auto& registry = scene::EntityRegistry::getRegistry();
                                                auto entity = services::internal::fromHandle(services::EntityHandle{
                                                    static_cast<uint64_t>(id)
                                                });
                                                if (!registry.valid(entity) || !registry.all_of<
                                                    components::RigidBodyComponent>(entity))
                                                {
                                                    return value::Value(1.0f);
                                                }

                                                return value::Value(
                                                    registry.get<components::RigidBodyComponent>(entity).mass);
                                            });

        // _native_physics_getLinearDamping(entityId) -> float
        interpreter->registerNativeFunction("_native_physics_getLinearDamping",
                                            [](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.empty())
                                                {
                                                    return value::Value(0.0f);
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                if (id < 0)
                                                {
                                                    return value::Value(0.0f);
                                                }

                                                auto& registry = scene::EntityRegistry::getRegistry();
                                                auto entity = services::internal::fromHandle(services::EntityHandle{
                                                    static_cast<uint64_t>(id)
                                                });
                                                if (!registry.valid(entity) || !registry.all_of<
                                                    components::RigidBodyComponent>(entity))
                                                {
                                                    return value::Value(0.0f);
                                                }

                                                return value::Value(
                                                    registry.get<components::RigidBodyComponent>(entity).linearDamping);
                                            });

        // _native_physics_getAngularDamping(entityId) -> float
        interpreter->registerNativeFunction("_native_physics_getAngularDamping",
                                            [](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.empty())
                                                {
                                                    return value::Value(0.05f);
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                if (id < 0)
                                                {
                                                    return value::Value(0.05f);
                                                }

                                                auto& registry = scene::EntityRegistry::getRegistry();
                                                auto entity = services::internal::fromHandle(services::EntityHandle{
                                                    static_cast<uint64_t>(id)
                                                });
                                                if (!registry.valid(entity) || !registry.all_of<
                                                    components::RigidBodyComponent>(entity))
                                                {
                                                    return value::Value(0.05f);
                                                }

                                                return value::Value(
                                                    registry.get<components::RigidBodyComponent>(
                                                        entity).angularDamping);
                                            });

        // _native_physics_getLinearVelocity(entityId) -> float[] (x, y, z)
        interpreter->registerNativeFunction("_native_physics_getLinearVelocity",
                                            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                auto result = std::make_shared<value::NativeArray>(
                                                    3, value::ValueType::FLOAT);
                                                result->set(0, value::Value(0.0f));
                                                result->set(1, value::Value(0.0f));
                                                result->set(2, value::Value(0.0f));

                                                if (args.empty())
                                                {
                                                    return value::Value(result);
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                if (id < 0)
                                                {
                                                    return value::Value(result);
                                                }

                                                events::physics::GetLinearVelocityQuery query;
                                                query.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                                                glm::vec3 vel = dispatcher.query(query);

                                                result->set(0, value::Value(vel.x));
                                                result->set(1, value::Value(vel.y));
                                                result->set(2, value::Value(vel.z));
                                                return value::Value(result);
                                            });

        // _native_physics_getAngularVelocity(entityId) -> float[] (x, y, z)
        interpreter->registerNativeFunction("_native_physics_getAngularVelocity",
                                            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                auto result = std::make_shared<value::NativeArray>(
                                                    3, value::ValueType::FLOAT);
                                                result->set(0, value::Value(0.0f));
                                                result->set(1, value::Value(0.0f));
                                                result->set(2, value::Value(0.0f));

                                                if (args.empty())
                                                {
                                                    return value::Value(result);
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                if (id < 0)
                                                {
                                                    return value::Value(result);
                                                }

                                                events::physics::GetAngularVelocityQuery query;
                                                query.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                                                glm::vec3 vel = dispatcher.query(query);

                                                result->set(0, value::Value(vel.x));
                                                result->set(1, value::Value(vel.y));
                                                result->set(2, value::Value(vel.z));
                                                return value::Value(result);
                                            });

        // ============================================
        // RigidBody Setters
        // ============================================

        // _native_physics_setBodyType(entityId, type) -> void
        interpreter->registerNativeFunction("_native_physics_setBodyType",
                                            [](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.size() < 2)
                                                {
                                                    return value::Value(std::monostate{});
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                int64_t type = extractInt64(args[1]);
                                                if (id < 0 || type < 0 || type > 2)
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                auto& registry = scene::EntityRegistry::getRegistry();
                                                auto entity = services::internal::fromHandle(services::EntityHandle{
                                                    static_cast<uint64_t>(id)
                                                });
                                                if (!registry.valid(entity) || !registry.all_of<
                                                    components::RigidBodyComponent>(entity))
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                registry.get<components::RigidBodyComponent>(entity).type =
                                                    static_cast<components::RigidBodyType>(type);
                                                return value::Value(std::monostate{});
                                            });

        // _native_physics_setMass(entityId, mass) -> void
        interpreter->registerNativeFunction("_native_physics_setMass",
                                            [](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.size() < 2)
                                                {
                                                    return value::Value(std::monostate{});
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                float mass = extractFloat(args[1]);
                                                if (id < 0 || mass < 0.0f)
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                auto& registry = scene::EntityRegistry::getRegistry();
                                                auto entity = services::internal::fromHandle(services::EntityHandle{
                                                    static_cast<uint64_t>(id)
                                                });
                                                if (!registry.valid(entity) || !registry.all_of<
                                                    components::RigidBodyComponent>(entity))
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                registry.get<components::RigidBodyComponent>(entity).mass = mass;
                                                return value::Value(std::monostate{});
                                            });

        // _native_physics_setLinearDamping(entityId, damping) -> void
        interpreter->registerNativeFunction("_native_physics_setLinearDamping",
                                            [](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.size() < 2)
                                                {
                                                    return value::Value(std::monostate{});
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                float damping = extractFloat(args[1]);
                                                if (id < 0)
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                auto& registry = scene::EntityRegistry::getRegistry();
                                                auto entity = services::internal::fromHandle(services::EntityHandle{
                                                    static_cast<uint64_t>(id)
                                                });
                                                if (!registry.valid(entity) || !registry.all_of<
                                                    components::RigidBodyComponent>(entity))
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                registry.get<components::RigidBodyComponent>(entity).linearDamping =
                                                    damping;
                                                return value::Value(std::monostate{});
                                            });

        // _native_physics_setAngularDamping(entityId, damping) -> void
        interpreter->registerNativeFunction("_native_physics_setAngularDamping",
                                            [](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.size() < 2)
                                                {
                                                    return value::Value(std::monostate{});
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                float damping = extractFloat(args[1]);
                                                if (id < 0)
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                auto& registry = scene::EntityRegistry::getRegistry();
                                                auto entity = services::internal::fromHandle(services::EntityHandle{
                                                    static_cast<uint64_t>(id)
                                                });
                                                if (!registry.valid(entity) || !registry.all_of<
                                                    components::RigidBodyComponent>(entity))
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                registry.get<components::RigidBodyComponent>(entity).angularDamping =
                                                    damping;
                                                return value::Value(std::monostate{});
                                            });

        // _native_physics_setLinearVelocity(entityId, x, y, z) -> void
        interpreter->registerNativeFunction("_native_physics_setLinearVelocity",
                                            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.size() < 4)
                                                {
                                                    return value::Value(std::monostate{});
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                float x = extractFloat(args[1]);
                                                float y = extractFloat(args[2]);
                                                float z = extractFloat(args[3]);
                                                if (id < 0)
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                events::physics::SetLinearVelocityCommand cmd;
                                                cmd.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                                                cmd.velocity = glm::vec3(x, y, z);
                                                dispatcher.execute(cmd);
                                                return value::Value(std::monostate{});
                                            });

        // _native_physics_setAngularVelocity(entityId, x, y, z) -> void
        interpreter->registerNativeFunction("_native_physics_setAngularVelocity",
                                            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.size() < 4)
                                                {
                                                    return value::Value(std::monostate{});
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                float x = extractFloat(args[1]);
                                                float y = extractFloat(args[2]);
                                                float z = extractFloat(args[3]);
                                                if (id < 0)
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                events::physics::SetAngularVelocityCommand cmd;
                                                cmd.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                                                cmd.velocity = glm::vec3(x, y, z);
                                                dispatcher.execute(cmd);
                                                return value::Value(std::monostate{});
                                            });

        // ============================================
        // Force and Impulse
        // ============================================

        // _native_physics_applyForce(entityId, x, y, z) -> void
        interpreter->registerNativeFunction("_native_physics_applyForce",
                                            [&dispatcher, &clampMagnitude]
                                        (const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.size() < 4)
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                // Rate limiting
                                                if (forceApplicationCountThisFrame >= MAX_FORCE_APPLICATIONS_PER_FRAME)
                                                {
                                                    vfLogWarning(
                                                        "[Script] Force application rate limit exceeded ({}/frame)",
                                                        MAX_FORCE_APPLICATIONS_PER_FRAME);
                                                    return value::Value(std::monostate{});
                                                }
                                                forceApplicationCountThisFrame++;

                                                int64_t id = extractInt64(args[0]);
                                                float x = extractFloat(args[1]);
                                                float y = extractFloat(args[2]);
                                                float z = extractFloat(args[3]);
                                                if (id < 0)
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                // Clamp force magnitude
                                                glm::vec3 force = clampMagnitude(
                                                    glm::vec3(x, y, z), MAX_FORCE_MAGNITUDE);

                                                events::physics::ApplyForceCommand cmd;
                                                cmd.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                                                cmd.force = force;
                                                dispatcher.execute(cmd);
                                                return value::Value(std::monostate{});
                                            });

        // _native_physics_applyForceAtPosition(entityId, fx, fy, fz, px, py, pz) -> void
        interpreter->registerNativeFunction("_native_physics_applyForceAtPosition",
                                            [&dispatcher, &clampMagnitude]
                                        (const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.size() < 7)
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                // Rate limiting
                                                if (forceApplicationCountThisFrame >= MAX_FORCE_APPLICATIONS_PER_FRAME)
                                                {
                                                    vfLogWarning(
                                                        "[Script] Force application rate limit exceeded ({}/frame)",
                                                        MAX_FORCE_APPLICATIONS_PER_FRAME);
                                                    return value::Value(std::monostate{});
                                                }
                                                forceApplicationCountThisFrame++;

                                                int64_t id = extractInt64(args[0]);
                                                float fx = extractFloat(args[1]);
                                                float fy = extractFloat(args[2]);
                                                float fz = extractFloat(args[3]);
                                                float px = extractFloat(args[4]);
                                                float py = extractFloat(args[5]);
                                                float pz = extractFloat(args[6]);
                                                if (id < 0)
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                // Clamp force magnitude
                                                glm::vec3 force = clampMagnitude(
                                                    glm::vec3(fx, fy, fz), MAX_FORCE_MAGNITUDE);

                                                events::physics::ApplyForceAtPositionCommand cmd;
                                                cmd.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                                                cmd.force = force;
                                                cmd.position = glm::vec3(px, py, pz);
                                                dispatcher.execute(cmd);
                                                return value::Value(std::monostate{});
                                            });

        // _native_physics_applyImpulse(entityId, x, y, z) -> void
        interpreter->registerNativeFunction("_native_physics_applyImpulse",
                                            [&dispatcher, &clampMagnitude]
                                        (const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.size() < 4)
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                // Rate limiting
                                                if (forceApplicationCountThisFrame >= MAX_FORCE_APPLICATIONS_PER_FRAME)
                                                {
                                                    vfLogWarning(
                                                        "[Script] Force/impulse application rate limit exceeded ({}/frame)",
                                                        MAX_FORCE_APPLICATIONS_PER_FRAME);
                                                    return value::Value(std::monostate{});
                                                }
                                                forceApplicationCountThisFrame++;

                                                int64_t id = extractInt64(args[0]);
                                                float x = extractFloat(args[1]);
                                                float y = extractFloat(args[2]);
                                                float z = extractFloat(args[3]);
                                                if (id < 0)
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                // Clamp impulse magnitude
                                                glm::vec3 impulse = clampMagnitude(
                                                    glm::vec3(x, y, z), MAX_IMPULSE_MAGNITUDE);

                                                events::physics::ApplyImpulseCommand cmd;
                                                cmd.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                                                cmd.impulse = impulse;
                                                dispatcher.execute(cmd);
                                                return value::Value(std::monostate{});
                                            });

        // _native_physics_applyTorque(entityId, x, y, z) -> void
        interpreter->registerNativeFunction("_native_physics_applyTorque",
                                            [&dispatcher, &clampMagnitude]
                                        (const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.size() < 4)
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                // Rate limiting (shares counter with forces/impulses)
                                                if (forceApplicationCountThisFrame >= MAX_FORCE_APPLICATIONS_PER_FRAME)
                                                {
                                                    vfLogWarning(
                                                        "[Script] Torque application rate limit exceeded ({}/frame)",
                                                        MAX_FORCE_APPLICATIONS_PER_FRAME);
                                                    return value::Value(std::monostate{});
                                                }
                                                forceApplicationCountThisFrame++;

                                                int64_t id = extractInt64(args[0]);
                                                float x = extractFloat(args[1]);
                                                float y = extractFloat(args[2]);
                                                float z = extractFloat(args[3]);
                                                if (id < 0)
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                // Clamp torque magnitude
                                                glm::vec3 torque = clampMagnitude(
                                                    glm::vec3(x, y, z), MAX_TORQUE_MAGNITUDE);

                                                events::physics::ApplyTorqueCommand cmd;
                                                cmd.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                                                cmd.torque = torque;
                                                dispatcher.execute(cmd);
                                                return value::Value(std::monostate{});
                                            });

        // ============================================
        // Physics Position/Rotation
        // ============================================

        // _native_physics_getPosition(entityId) -> float[] (x, y, z)
        interpreter->registerNativeFunction("_native_physics_getPosition",
                                            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                auto result = std::make_shared<value::NativeArray>(
                                                    3, value::ValueType::FLOAT);
                                                result->set(0, value::Value(0.0f));
                                                result->set(1, value::Value(0.0f));
                                                result->set(2, value::Value(0.0f));

                                                if (args.empty())
                                                {
                                                    return value::Value(result);
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                if (id < 0)
                                                {
                                                    return value::Value(result);
                                                }

                                                events::physics::GetPhysicsPositionQuery query;
                                                query.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                                                glm::vec3 pos = dispatcher.query(query);

                                                result->set(0, value::Value(pos.x));
                                                result->set(1, value::Value(pos.y));
                                                result->set(2, value::Value(pos.z));
                                                return value::Value(result);
                                            });

        // _native_physics_setPosition(entityId, x, y, z) -> void
        interpreter->registerNativeFunction("_native_physics_setPosition",
                                            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.size() < 4)
                                                {
                                                    return value::Value(std::monostate{});
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                float x = extractFloat(args[1]);
                                                float y = extractFloat(args[2]);
                                                float z = extractFloat(args[3]);
                                                if (id < 0)
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                events::physics::SetPhysicsPositionCommand cmd;
                                                cmd.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                                                cmd.position = glm::vec3(x, y, z);
                                                dispatcher.execute(cmd);
                                                return value::Value(std::monostate{});
                                            });

        // _native_physics_getRotation(entityId) -> float[] (x, y, z, w) quaternion
        interpreter->registerNativeFunction("_native_physics_getRotation",
                                            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                auto result = std::make_shared<value::NativeArray>(
                                                    4, value::ValueType::FLOAT);
                                                result->set(0, value::Value(0.0f));
                                                result->set(1, value::Value(0.0f));
                                                result->set(2, value::Value(0.0f));
                                                result->set(3, value::Value(1.0f));

                                                if (args.empty())
                                                {
                                                    return value::Value(result);
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                if (id < 0)
                                                {
                                                    return value::Value(result);
                                                }

                                                events::physics::GetPhysicsRotationQuery query;
                                                query.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                                                glm::quat rot = dispatcher.query(query);

                                                result->set(0, value::Value(rot.x));
                                                result->set(1, value::Value(rot.y));
                                                result->set(2, value::Value(rot.z));
                                                result->set(3, value::Value(rot.w));
                                                return value::Value(result);
                                            });

        // _native_physics_setRotation(entityId, x, y, z, w) -> void
        interpreter->registerNativeFunction("_native_physics_setRotation",
                                            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.size() < 5)
                                                {
                                                    return value::Value(std::monostate{});
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                float x = extractFloat(args[1]);
                                                float y = extractFloat(args[2]);
                                                float z = extractFloat(args[3]);
                                                float w = extractFloat(args[4]);
                                                if (id < 0)
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                events::physics::SetPhysicsRotationCommand cmd;
                                                cmd.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                                                cmd.rotation = glm::quat(w, x, y, z);
                                                dispatcher.execute(cmd);
                                                return value::Value(std::monostate{});
                                            });

        // ============================================
        // Collider Queries
        // ============================================

        // _native_physics_hasCollider(entityId) -> bool
        interpreter->registerNativeFunction("_native_physics_hasCollider",
                                            [](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.empty())
                                                {
                                                    return value::Value(false);
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                if (id < 0)
                                                {
                                                    return value::Value(false);
                                                }

                                                auto& registry = scene::EntityRegistry::getRegistry();
                                                auto entity = services::internal::fromHandle(services::EntityHandle{
                                                    static_cast<uint64_t>(id)
                                                });
                                                if (!registry.valid(entity))
                                                {
                                                    return value::Value(false);
                                                }

                                                return value::Value(
                                                    registry.all_of<components::ColliderComponent>(entity));
                                            });

        // _native_physics_getColliderShape(entityId) -> int
        interpreter->registerNativeFunction("_native_physics_getColliderShape",
                                            [](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.empty())
                                                {
                                                    return value::Value(static_cast<int64_t>(0)); // Box default
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                if (id < 0)
                                                {
                                                    return value::Value(static_cast<int64_t>(0));
                                                }

                                                auto& registry = scene::EntityRegistry::getRegistry();
                                                auto entity = services::internal::fromHandle(services::EntityHandle{
                                                    static_cast<uint64_t>(id)
                                                });
                                                if (!registry.valid(entity) || !registry.all_of<
                                                    components::ColliderComponent>(entity))
                                                {
                                                    return value::Value(static_cast<int64_t>(0));
                                                }

                                                return value::Value(
                                                    static_cast<int64_t>(registry.get<components::ColliderComponent>(
                                                        entity).shape));
                                            });

        // _native_physics_getColliderSize(entityId) -> float[] (x, y, z)
        interpreter->registerNativeFunction("_native_physics_getColliderSize",
                                            [](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                auto result = std::make_shared<value::NativeArray>(
                                                    3, value::ValueType::FLOAT);
                                                result->set(0, value::Value(1.0f));
                                                result->set(1, value::Value(1.0f));
                                                result->set(2, value::Value(1.0f));

                                                if (args.empty())
                                                {
                                                    return value::Value(result);
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                if (id < 0)
                                                {
                                                    return value::Value(result);
                                                }

                                                auto& registry = scene::EntityRegistry::getRegistry();
                                                auto entity = services::internal::fromHandle(services::EntityHandle{
                                                    static_cast<uint64_t>(id)
                                                });
                                                if (!registry.valid(entity) || !registry.all_of<
                                                    components::ColliderComponent>(entity))
                                                {
                                                    return value::Value(result);
                                                }

                                                auto& col = registry.get<components::ColliderComponent>(entity);
                                                result->set(0, value::Value(col.size.x));
                                                result->set(1, value::Value(col.size.y));
                                                result->set(2, value::Value(col.size.z));
                                                return value::Value(result);
                                            });

        // _native_physics_getColliderHeight(entityId) -> float (capsule height)
        interpreter->registerNativeFunction("_native_physics_getColliderHeight",
                                            [](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.empty())
                                                {
                                                    return value::Value(2.0f);
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                if (id < 0)
                                                {
                                                    return value::Value(2.0f);
                                                }

                                                auto& registry = scene::EntityRegistry::getRegistry();
                                                auto entity = services::internal::fromHandle(services::EntityHandle{
                                                    static_cast<uint64_t>(id)
                                                });
                                                if (!registry.valid(entity) || !registry.all_of<
                                                    components::ColliderComponent>(entity))
                                                {
                                                    return value::Value(2.0f);
                                                }

                                                return value::Value(
                                                    registry.get<components::ColliderComponent>(entity).height);
                                            });

        // _native_physics_getColliderOffset(entityId) -> float[] (x, y, z)
        interpreter->registerNativeFunction("_native_physics_getColliderOffset",
                                            [](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                auto result = std::make_shared<value::NativeArray>(
                                                    3, value::ValueType::FLOAT);
                                                result->set(0, value::Value(0.0f));
                                                result->set(1, value::Value(0.0f));
                                                result->set(2, value::Value(0.0f));

                                                if (args.empty())
                                                {
                                                    return value::Value(result);
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                if (id < 0)
                                                {
                                                    return value::Value(result);
                                                }

                                                auto& registry = scene::EntityRegistry::getRegistry();
                                                auto entity = services::internal::fromHandle(services::EntityHandle{
                                                    static_cast<uint64_t>(id)
                                                });
                                                if (!registry.valid(entity) || !registry.all_of<
                                                    components::ColliderComponent>(entity))
                                                {
                                                    return value::Value(result);
                                                }

                                                auto& col = registry.get<components::ColliderComponent>(entity);
                                                result->set(0, value::Value(col.offset.x));
                                                result->set(1, value::Value(col.offset.y));
                                                result->set(2, value::Value(col.offset.z));
                                                return value::Value(result);
                                            });

        // _native_physics_isTrigger(entityId) -> bool
        interpreter->registerNativeFunction("_native_physics_isTrigger",
                                            [](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.empty())
                                                {
                                                    return value::Value(false);
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                if (id < 0)
                                                {
                                                    return value::Value(false);
                                                }

                                                auto& registry = scene::EntityRegistry::getRegistry();
                                                auto entity = services::internal::fromHandle(services::EntityHandle{
                                                    static_cast<uint64_t>(id)
                                                });
                                                if (!registry.valid(entity) || !registry.all_of<
                                                    components::ColliderComponent>(entity))
                                                {
                                                    return value::Value(false);
                                                }

                                                return value::Value(
                                                    registry.get<components::ColliderComponent>(entity).isTrigger);
                                            });

        // _native_physics_getCollisionLayer(entityId) -> int
        interpreter->registerNativeFunction("_native_physics_getCollisionLayer",
                                            [](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.empty())
                                                {
                                                    return value::Value(static_cast<int64_t>(1));
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                if (id < 0)
                                                {
                                                    return value::Value(static_cast<int64_t>(1));
                                                }

                                                auto& registry = scene::EntityRegistry::getRegistry();
                                                auto entity = services::internal::fromHandle(services::EntityHandle{
                                                    static_cast<uint64_t>(id)
                                                });
                                                if (!registry.valid(entity) || !registry.all_of<
                                                    components::ColliderComponent>(entity))
                                                {
                                                    return value::Value(static_cast<int64_t>(1));
                                                }

                                                return value::Value(
                                                    static_cast<int64_t>(registry.get<components::ColliderComponent>(
                                                        entity).collisionLayer));
                                            });

        // _native_physics_getFriction(entityId) -> float
        interpreter->registerNativeFunction("_native_physics_getFriction",
                                            [](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.empty())
                                                {
                                                    return value::Value(0.5f);
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                if (id < 0)
                                                {
                                                    return value::Value(0.5f);
                                                }

                                                auto& registry = scene::EntityRegistry::getRegistry();
                                                auto entity = services::internal::fromHandle(services::EntityHandle{
                                                    static_cast<uint64_t>(id)
                                                });
                                                if (!registry.valid(entity) || !registry.all_of<
                                                    components::ColliderComponent>(entity))
                                                {
                                                    return value::Value(0.5f);
                                                }

                                                return value::Value(
                                                    registry.get<components::ColliderComponent>(entity).friction);
                                            });

        // _native_physics_getRestitution(entityId) -> float
        interpreter->registerNativeFunction("_native_physics_getRestitution",
                                            [](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.empty())
                                                {
                                                    return value::Value(0.0f);
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                if (id < 0)
                                                {
                                                    return value::Value(0.0f);
                                                }

                                                auto& registry = scene::EntityRegistry::getRegistry();
                                                auto entity = services::internal::fromHandle(services::EntityHandle{
                                                    static_cast<uint64_t>(id)
                                                });
                                                if (!registry.valid(entity) || !registry.all_of<
                                                    components::ColliderComponent>(entity))
                                                {
                                                    return value::Value(0.0f);
                                                }

                                                return value::Value(
                                                    registry.get<components::ColliderComponent>(entity).restitution);
                                            });

        // ============================================
        // Collider Setters
        // ============================================

        // _native_physics_setColliderSize(entityId, x, y, z) -> void
        interpreter->registerNativeFunction("_native_physics_setColliderSize",
                                            [](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.size() < 4)
                                                {
                                                    return value::Value(std::monostate{});
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                float x = extractFloat(args[1]);
                                                float y = extractFloat(args[2]);
                                                float z = extractFloat(args[3]);
                                                if (id < 0)
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                auto& registry = scene::EntityRegistry::getRegistry();
                                                auto entity = services::internal::fromHandle(services::EntityHandle{
                                                    static_cast<uint64_t>(id)
                                                });
                                                if (!registry.valid(entity) || !registry.all_of<
                                                    components::ColliderComponent>(entity))
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                registry.get<components::ColliderComponent>(entity).size = glm::vec3(
                                                    x, y, z);
                                                return value::Value(std::monostate{});
                                            });

        // _native_physics_setColliderHeight(entityId, height) -> void
        interpreter->registerNativeFunction("_native_physics_setColliderHeight",
                                            [](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.size() < 2)
                                                {
                                                    return value::Value(std::monostate{});
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                float height = extractFloat(args[1]);
                                                if (id < 0)
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                auto& registry = scene::EntityRegistry::getRegistry();
                                                auto entity = services::internal::fromHandle(services::EntityHandle{
                                                    static_cast<uint64_t>(id)
                                                });
                                                if (!registry.valid(entity) || !registry.all_of<
                                                    components::ColliderComponent>(entity))
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                registry.get<components::ColliderComponent>(entity).height = height;
                                                return value::Value(std::monostate{});
                                            });

        // _native_physics_setTrigger(entityId, isTrigger) -> void
        interpreter->registerNativeFunction("_native_physics_setTrigger",
                                            [](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.size() < 2)
                                                {
                                                    return value::Value(std::monostate{});
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                bool isTrigger = std::holds_alternative<bool>(args[1])
                                                                     ? std::get<bool>(args[1])
                                                                     : false;
                                                if (id < 0)
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                auto& registry = scene::EntityRegistry::getRegistry();
                                                auto entity = services::internal::fromHandle(services::EntityHandle{
                                                    static_cast<uint64_t>(id)
                                                });
                                                if (!registry.valid(entity) || !registry.all_of<
                                                    components::ColliderComponent>(entity))
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                registry.get<components::ColliderComponent>(entity).isTrigger =
                                                    isTrigger;
                                                return value::Value(std::monostate{});
                                            });

        // _native_physics_setCollisionLayer(entityId, layer) -> void
        interpreter->registerNativeFunction("_native_physics_setCollisionLayer",
                                            [](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.size() < 2)
                                                {
                                                    return value::Value(std::monostate{});
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                int64_t layer = extractInt64(args[1]);
                                                if (id < 0 || layer < 0 || layer > 15)
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                auto& registry = scene::EntityRegistry::getRegistry();
                                                auto entity = services::internal::fromHandle(services::EntityHandle{
                                                    static_cast<uint64_t>(id)
                                                });
                                                if (!registry.valid(entity) || !registry.all_of<
                                                    components::ColliderComponent>(entity))
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                registry.get<components::ColliderComponent>(entity).collisionLayer =
                                                    static_cast<uint8_t>(layer);
                                                return value::Value(std::monostate{});
                                            });

        // _native_physics_setFriction(entityId, friction) -> void
        interpreter->registerNativeFunction("_native_physics_setFriction",
                                            [](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.size() < 2)
                                                {
                                                    return value::Value(std::monostate{});
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                float friction = extractFloat(args[1]);
                                                if (id < 0)
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                auto& registry = scene::EntityRegistry::getRegistry();
                                                auto entity = services::internal::fromHandle(services::EntityHandle{
                                                    static_cast<uint64_t>(id)
                                                });
                                                if (!registry.valid(entity) || !registry.all_of<
                                                    components::ColliderComponent>(entity))
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                registry.get<components::ColliderComponent>(entity).friction = friction;
                                                return value::Value(std::monostate{});
                                            });

        // _native_physics_setRestitution(entityId, restitution) -> void
        interpreter->registerNativeFunction("_native_physics_setRestitution",
                                            [](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.size() < 2)
                                                {
                                                    return value::Value(std::monostate{});
                                                }
                                                int64_t id = extractInt64(args[0]);
                                                float restitution = extractFloat(args[1]);
                                                if (id < 0)
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                auto& registry = scene::EntityRegistry::getRegistry();
                                                auto entity = services::internal::fromHandle(services::EntityHandle{
                                                    static_cast<uint64_t>(id)
                                                });
                                                if (!registry.valid(entity) || !registry.all_of<
                                                    components::ColliderComponent>(entity))
                                                {
                                                    return value::Value(std::monostate{});
                                                }

                                                registry.get<components::ColliderComponent>(entity).restitution =
                                                    restitution;
                                                return value::Value(std::monostate{});
                                            });

        // ============================================
        // Raycasting
        // ============================================

        // _native_physics_raycast(ox, oy, oz, dx, dy, dz, maxDist) -> float[]
        // Returns: [hit(0/1), entityId, px, py, pz, nx, ny, nz, distance]
        interpreter->registerNativeFunction("_native_physics_raycast",
                                            [&dispatcher]
                                        (const std::vector<value::Value>& args) -> value::Value
                                            {
                                                // Rate limiting for raycasts
                                                if (raycastCountThisFrame >= MAX_RAYCASTS_PER_FRAME)
                                                {
                                                    vfLogWarning("[Script] Raycast rate limit exceeded ({}/frame)",
                                                                 MAX_RAYCASTS_PER_FRAME);
                                                    auto result = std::make_shared<value::NativeArray>(
                                                        1, value::ValueType::FLOAT);
                                                    result->set(0, value::Value(0.0f)); // No hit (rate limited)
                                                    return value::Value(result);
                                                }
                                                raycastCountThisFrame++;

                                                if (args.size() < 7)
                                                {
                                                    auto result = std::make_shared<value::NativeArray>(
                                                        1, value::ValueType::FLOAT);
                                                    result->set(0, value::Value(0.0f)); // No hit
                                                    return value::Value(result);
                                                }

                                                float ox = extractFloat(args[0]);
                                                float oy = extractFloat(args[1]);
                                                float oz = extractFloat(args[2]);
                                                float dx = extractFloat(args[3]);
                                                float dy = extractFloat(args[4]);
                                                float dz = extractFloat(args[5]);
                                                float maxDist = extractFloat(args[6]);

                                                // Clamp max distance to prevent expensive long-range queries
                                                maxDist = std::min(std::max(0.0f, maxDist), MAX_RAYCAST_DISTANCE);

                                                events::physics::RaycastQuery query;
                                                query.origin = glm::vec3(ox, oy, oz);
                                                query.direction = glm::vec3(dx, dy, dz);
                                                query.maxDistance = maxDist;
                                                services::RaycastHit hit = dispatcher.query(query);

                                                if (hit.hit)
                                                {
                                                    auto result = std::make_shared<value::NativeArray>(
                                                        9, value::ValueType::FLOAT);
                                                    result->set(0, value::Value(1.0f)); // hit
                                                    result->set(1, value::Value(static_cast<float>(hit.entity.id)));
                                                    result->set(2, value::Value(hit.point.x));
                                                    result->set(3, value::Value(hit.point.y));
                                                    result->set(4, value::Value(hit.point.z));
                                                    result->set(5, value::Value(hit.normal.x));
                                                    result->set(6, value::Value(hit.normal.y));
                                                    result->set(7, value::Value(hit.normal.z));
                                                    result->set(8, value::Value(hit.distance));
                                                    return value::Value(result);
                                                }
                                                else
                                                {
                                                    auto result = std::make_shared<value::NativeArray>(
                                                        1, value::ValueType::FLOAT);
                                                    result->set(0, value::Value(0.0f)); // No hit
                                                    return value::Value(result);
                                                }
                                            });

        // _native_physics_isOverlapping(entityA, entityB) -> bool
        interpreter->registerNativeFunction("_native_physics_isOverlapping",
                                            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.size() < 2)
                                                {
                                                    return value::Value(false);
                                                }
                                                int64_t idA = extractInt64(args[0]);
                                                int64_t idB = extractInt64(args[1]);
                                                if (idA < 0 || idB < 0)
                                                {
                                                    return value::Value(false);
                                                }

                                                events::physics::IsOverlappingQuery query;
                                                query.entityA = services::EntityHandle{static_cast<uint64_t>(idA)};
                                                query.entityB = services::EntityHandle{static_cast<uint64_t>(idB)};
                                                return value::Value(dispatcher.query(query));
                                            });

        // ============================================
        // Global Physics Settings
        // ============================================

        // _native_physics_getGravity() -> float[] (x, y, z)
        interpreter->registerNativeFunction("_native_physics_getGravity",
                                            [&dispatcher](const std::vector<value::Value>&) -> value::Value
                                            {
                                                auto result = std::make_shared<value::NativeArray>(
                                                    3, value::ValueType::FLOAT);

                                                events::physics::GetGravityQuery query;
                                                glm::vec3 gravity = dispatcher.query(query);

                                                result->set(0, value::Value(gravity.x));
                                                result->set(1, value::Value(gravity.y));
                                                result->set(2, value::Value(gravity.z));
                                                return value::Value(result);
                                            });

        // _native_physics_setGravity(x, y, z) -> void
        interpreter->registerNativeFunction("_native_physics_setGravity",
                                            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.size() < 3)
                                                {
                                                    return value::Value(std::monostate{});
                                                }
                                                float x = extractFloat(args[0]);
                                                float y = extractFloat(args[1]);
                                                float z = extractFloat(args[2]);

                                                events::physics::SetGravityCommand cmd;
                                                cmd.gravity = glm::vec3(x, y, z);
                                                dispatcher.execute(cmd);
                                                return value::Value(std::monostate{});
                                            });

        vfLogInfo("[PhysicsAPI] Registered Physics native functions");
    }
}

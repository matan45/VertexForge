// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "PhysicsAPI.hpp"
#include "PhysicsRigidBodyAPI.hpp"
#include "PhysicsColliderAPI.hpp"
#include "PhysicsAnimationAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/physics/PhysicsEvents.hpp"

namespace core::api
{
    void PhysicsAPI::beginFrame()
    {
        raycastCountThisFrame = 0;
        PhysicsRigidBodyAPI::beginFrame();
    }

    void PhysicsAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        PhysicsRigidBodyAPI::registerAPI(interpreter);
        PhysicsColliderAPI::registerAPI(interpreter);
        PhysicsAnimationAPI::registerAPI(interpreter);

        interpreter->registerNativeFunction("_native_physics_raycast",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (raycastCountThisFrame >= MAX_RAYCASTS_PER_FRAME)
                {
                    vfLogWarning("[Script] Raycast rate limit exceeded ({}/frame)",
                                 MAX_RAYCASTS_PER_FRAME);
                    auto result = std::make_shared<value::NativeArray>(1, value::ValueType::FLOAT);
                    result->set(0, value::Value(0.0f));
                    return value::Value(result);
                }
                raycastCountThisFrame++;

                if (args.size() < 7)
                {
                    auto result = std::make_shared<value::NativeArray>(1, value::ValueType::FLOAT);
                    result->set(0, value::Value(0.0f));
                    return value::Value(result);
                }

                float maxDist = std::min(std::max(0.0f, extractFloat(args[6])),
                                         MAX_RAYCAST_DISTANCE);

                events::physics::RaycastQuery query;
                query.origin = glm::vec3(extractFloat(args[0]), extractFloat(args[1]),
                                         extractFloat(args[2]));
                query.direction = glm::vec3(extractFloat(args[3]), extractFloat(args[4]),
                                            extractFloat(args[5]));
                query.maxDistance = maxDist;
                services::RaycastHit hit = dispatcher.query(query);

                if (hit.hit)
                {
                    auto result = std::make_shared<value::NativeArray>(9, value::ValueType::FLOAT);
                    result->set(0, value::Value(1.0f));
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

                auto result = std::make_shared<value::NativeArray>(1, value::ValueType::FLOAT);
                result->set(0, value::Value(0.0f));
                return value::Value(result);
            });

        interpreter->registerNativeFunction("_native_physics_isOverlapping",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2) return value::Value(false);
                int64_t idA = extractInt64(args[0]);
                int64_t idB = extractInt64(args[1]);
                if (idA < 0 || idB < 0) return value::Value(false);

                events::physics::IsOverlappingQuery query;
                query.entityA = services::EntityHandle{static_cast<uint64_t>(idA)};
                query.entityB = services::EntityHandle{static_cast<uint64_t>(idB)};
                return value::Value(dispatcher.query(query));
            });

        interpreter->registerNativeFunction("_native_physics_getGravity",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                events::physics::GetGravityQuery query;
                return makeVec3Array(dispatcher.query(query));
            });

        interpreter->registerNativeFunction("_native_physics_setGravity",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 3) return value::Value(std::monostate{});
                events::physics::SetGravityCommand cmd;
                cmd.gravity = glm::vec3(extractFloat(args[0]), extractFloat(args[1]),
                                        extractFloat(args[2]));
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        vfLogInfo("[PhysicsAPI] Registered Physics native functions");
    }
}

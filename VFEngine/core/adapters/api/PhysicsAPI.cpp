// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "PhysicsAPI.hpp"
#include "PhysicsRigidBodyAPI.hpp"
#include "PhysicsColliderAPI.hpp"
#include "PhysicsAnimationAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/physics/PhysicsEvents.hpp"
#include "../../../services/events/physics/PhysicsSettingsEvents.hpp"

namespace core::api
{
    namespace
    {
        // Resolve comma-separated layer names (e.g. "Dynamic,Sensor") to a uint16_t bitmask.
        // Returns 0xFFFF (all layers) if the string is empty.
        uint16_t resolveLayerMask(const std::string& layerNames)
        {
            if (layerNames.empty()) return 0xFFFF;

            auto& dispatcher = events::EventDispatcher::instance();
            events::physics::GetPhysicsSettingsQuery settingsQuery;
            auto settings = dispatcher.query(settingsQuery);

            uint16_t mask = 0;
            size_t start = 0;
            while (start < layerNames.size())
            {
                size_t end = layerNames.find(',', start);
                if (end == std::string::npos) end = layerNames.size();

                // Trim whitespace
                size_t nameStart = start;
                size_t nameEnd = end;
                while (nameStart < nameEnd && layerNames[nameStart] == ' ') ++nameStart;
                while (nameEnd > nameStart && layerNames[nameEnd - 1] == ' ') --nameEnd;

                std::string name = layerNames.substr(nameStart, nameEnd - nameStart);
                if (!name.empty())
                {
                    const auto* layer = settings.getLayerByName(name);
                    if (layer)
                    {
                        mask |= (1u << layer->index);
                    }
                    else
                    {
                        vfLogWarning("[Script] Unknown collision layer name: '{}'", name);
                    }
                }
                start = end + 1;
            }

            return mask == 0 ? 0xFFFF : mask;
        }
    }

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
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
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

                uint16_t layerMask = 0xFFFF;
                if (args.size() >= 8)
                {
                    layerMask = resolveLayerMask(extractString(args[7]));
                }

                events::physics::RaycastQuery query;
                query.origin = glm::vec3(extractFloat(args[0]), extractFloat(args[1]),
                                         extractFloat(args[2]));
                query.direction = glm::vec3(extractFloat(args[3]), extractFloat(args[4]),
                                            extractFloat(args[5]));
                query.maxDistance = maxDist;
                query.layerMask = layerMask;
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
            }});

        interpreter->registerNativeFunction("_native_physics_raycastAll",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
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

                uint16_t layerMask = 0xFFFF;
                if (args.size() >= 8)
                {
                    layerMask = resolveLayerMask(extractString(args[7]));
                }

                events::physics::RaycastAllQuery query;
                query.origin = glm::vec3(extractFloat(args[0]), extractFloat(args[1]),
                                         extractFloat(args[2]));
                query.direction = glm::vec3(extractFloat(args[3]), extractFloat(args[4]),
                                            extractFloat(args[5]));
                query.maxDistance = maxDist;
                query.layerMask = layerMask;
                auto hits = dispatcher.query(query);

                // Pack as flat float array: [hitCount, entityId0, x0, y0, z0, nx0, ny0, nz0, dist0, entityId1, ...]
                // Each hit = 8 floats (entityId, px, py, pz, nx, ny, nz, distance)
                auto result = std::make_shared<value::NativeArray>(
                    1 + hits.size() * 8, value::ValueType::FLOAT);
                result->set(0, value::Value(static_cast<float>(hits.size())));

                for (size_t i = 0; i < hits.size(); ++i)
                {
                    size_t base = 1 + i * 8;
                    result->set(base + 0, value::Value(static_cast<float>(hits[i].entity.id)));
                    result->set(base + 1, value::Value(hits[i].point.x));
                    result->set(base + 2, value::Value(hits[i].point.y));
                    result->set(base + 3, value::Value(hits[i].point.z));
                    result->set(base + 4, value::Value(hits[i].normal.x));
                    result->set(base + 5, value::Value(hits[i].normal.y));
                    result->set(base + 6, value::Value(hits[i].normal.z));
                    result->set(base + 7, value::Value(hits[i].distance));
                }

                return value::Value(result);
            }});

        interpreter->registerNativeFunction("_native_physics_isOverlapping",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(false);
                int64_t idA = extractInt64(args[0]);
                int64_t idB = extractInt64(args[1]);
                if (idA < 0 || idB < 0) return value::Value(false);

                events::physics::IsOverlappingQuery query;
                query.entityA = services::EntityHandle{static_cast<uint64_t>(idA)};
                query.entityB = services::EntityHandle{static_cast<uint64_t>(idB)};
                return value::Value(dispatcher.query(query));
            }});

        interpreter->registerNativeFunction("_native_physics_getGravity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                events::physics::GetGravityQuery query;
                return makeVec3Array(dispatcher.query(query));
            }});

        interpreter->registerNativeFunction("_native_physics_setGravity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 3) return value::Value(std::monostate{});
                events::physics::SetGravityCommand cmd;
                cmd.gravity = glm::vec3(extractFloat(args[0]), extractFloat(args[1]),
                                        extractFloat(args[2]));
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        vfLogInfo("[PhysicsAPI] Registered Physics native functions");
    }
}

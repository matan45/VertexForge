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

        // Pack a list of entity handles into a pure-int NativeArray (the int[] idiom
        // used by Entity.findAll / _plugin_findAll). Length 0 on no hits.
        value::Value makeEntityIdArray(const std::vector<services::EntityHandle>& entities)
        {
            auto arr = std::make_shared<value::NativeArray>(entities.size(), value::ValueType::INT);
            for (size_t i = 0; i < entities.size(); ++i)
            {
                arr->set(i, value::Value(entityToInt(entities[i])));
            }
            return value::Value(arr);
        }
    }

    void PhysicsAPI::beginFrame()
    {
        raycastCountThisFrame = 0;
        overlapCountThisFrame = 0;
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

        // overlapSphere(cx, cy, cz, radius [, layers]) -> int[] entity ids
        interpreter->registerNativeFunction("_native_physics_overlapSphere",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (overlapCountThisFrame >= MAX_OVERLAPS_PER_FRAME)
                {
                    vfLogWarning("[Script] Overlap query rate limit exceeded ({}/frame)",
                                 MAX_OVERLAPS_PER_FRAME);
                    return value::Value(std::make_shared<value::NativeArray>(0, value::ValueType::INT));
                }
                overlapCountThisFrame++;

                if (args.size() < 4)
                {
                    return value::Value(std::make_shared<value::NativeArray>(0, value::ValueType::INT));
                }

                events::physics::OverlapSphereQuery query;
                query.center = glm::vec3(extractFloat(args[0]), extractFloat(args[1]),
                                         extractFloat(args[2]));
                query.radius = extractFloat(args[3]);
                if (args.size() >= 5)
                {
                    query.layerMask = resolveLayerMask(extractString(args[4]));
                }
                return makeEntityIdArray(dispatcher.query(query));
            }});

        // overlapBox(cx, cy, cz, hx, hy, hz, qx, qy, qz, qw [, layers]) -> int[] entity ids
        interpreter->registerNativeFunction("_native_physics_overlapBox",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (overlapCountThisFrame >= MAX_OVERLAPS_PER_FRAME)
                {
                    vfLogWarning("[Script] Overlap query rate limit exceeded ({}/frame)",
                                 MAX_OVERLAPS_PER_FRAME);
                    return value::Value(std::make_shared<value::NativeArray>(0, value::ValueType::INT));
                }
                overlapCountThisFrame++;

                if (args.size() < 10)
                {
                    return value::Value(std::make_shared<value::NativeArray>(0, value::ValueType::INT));
                }

                events::physics::OverlapBoxQuery query;
                query.center = glm::vec3(extractFloat(args[0]), extractFloat(args[1]),
                                         extractFloat(args[2]));
                query.halfExtents = glm::vec3(extractFloat(args[3]), extractFloat(args[4]),
                                              extractFloat(args[5]));
                // mType Quaternion is (x, y, z, w); glm::quat ctor is (w, x, y, z).
                query.rotation = glm::quat(extractFloat(args[9]), extractFloat(args[6]),
                                           extractFloat(args[7]), extractFloat(args[8]));
                if (args.size() >= 11)
                {
                    query.layerMask = resolveLayerMask(extractString(args[10]));
                }
                return makeEntityIdArray(dispatcher.query(query));
            }});

        // overlapCapsule(cx, cy, cz, halfHeight, radius, qx, qy, qz, qw [, layers]) -> int[] entity ids
        interpreter->registerNativeFunction("_native_physics_overlapCapsule",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (overlapCountThisFrame >= MAX_OVERLAPS_PER_FRAME)
                {
                    vfLogWarning("[Script] Overlap query rate limit exceeded ({}/frame)",
                                 MAX_OVERLAPS_PER_FRAME);
                    return value::Value(std::make_shared<value::NativeArray>(0, value::ValueType::INT));
                }
                overlapCountThisFrame++;

                if (args.size() < 9)
                {
                    return value::Value(std::make_shared<value::NativeArray>(0, value::ValueType::INT));
                }

                events::physics::OverlapCapsuleQuery query;
                query.center = glm::vec3(extractFloat(args[0]), extractFloat(args[1]),
                                         extractFloat(args[2]));
                query.halfHeight = extractFloat(args[3]);
                query.radius = extractFloat(args[4]);
                // mType Quaternion is (x, y, z, w); glm::quat ctor is (w, x, y, z).
                query.rotation = glm::quat(extractFloat(args[8]), extractFloat(args[5]),
                                           extractFloat(args[6]), extractFloat(args[7]));
                if (args.size() >= 10)
                {
                    query.layerMask = resolveLayerMask(extractString(args[9]));
                }
                return makeEntityIdArray(dispatcher.query(query));
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

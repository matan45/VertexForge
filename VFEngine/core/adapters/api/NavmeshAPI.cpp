// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "NavmeshAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/navmesh/NavmeshEvents.hpp"
#include "../../../services/events/volumetric/VolumetricNavEvents.hpp"
#include "../../../services/events/ai/EQSEvents.hpp"

namespace core::api
{
    void NavmeshAPI::beginFrame()
    {
        pathQueryCountThisFrame = 0;
    }

    void NavmeshAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        interpreter->registerNativeFunction("_native_navmesh_findPath",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (pathQueryCountThisFrame >= MAX_PATH_QUERIES_PER_FRAME)
                {
                    vfLogWarning("[Script] Navmesh path query rate limit exceeded ({}/frame)",
                                 MAX_PATH_QUERIES_PER_FRAME);
                    auto result = std::make_shared<value::NativeArray>(1, value::ValueType::FLOAT);
                    result->set(0, value::Value(0.0f));
                    return value::Value(result);
                }
                pathQueryCountThisFrame++;

                if (args.size() < 6)
                {
                    auto result = std::make_shared<value::NativeArray>(1, value::ValueType::FLOAT);
                    result->set(0, value::Value(0.0f));
                    return value::Value(result);
                }

                events::navmesh::FindPathQuery query;
                query.start = glm::vec3(extractFloat(args[0]), extractFloat(args[1]),
                                         extractFloat(args[2]));
                query.end = glm::vec3(extractFloat(args[3]), extractFloat(args[4]),
                                       extractFloat(args[5]));

                auto path = dispatcher.query(query);

                if (!path.isValid || path.waypoints.empty())
                {
                    auto result = std::make_shared<value::NativeArray>(1, value::ValueType::FLOAT);
                    result->set(0, value::Value(0.0f));
                    return value::Value(result);
                }

                // Return: [waypointCount, x0, y0, z0, x1, y1, z1, ...]
                size_t count = path.waypoints.size();
                auto result = std::make_shared<value::NativeArray>(
                    1 + static_cast<int>(count * 3), value::ValueType::FLOAT);
                result->set(0, value::Value(static_cast<float>(count)));
                for (size_t i = 0; i < count; ++i)
                {
                    result->set(static_cast<int>(1 + i * 3), value::Value(path.waypoints[i].x));
                    result->set(static_cast<int>(2 + i * 3), value::Value(path.waypoints[i].y));
                    result->set(static_cast<int>(3 + i * 3), value::Value(path.waypoints[i].z));
                }
                return value::Value(result);
            }});

        interpreter->registerNativeFunction("_native_navmesh_setDestination",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 4) return value::Value(std::monostate{});

                events::navmesh::SetAgentDestinationCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.target = glm::vec3(extractFloat(args[1]), extractFloat(args[2]),
                                        extractFloat(args[3]));
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_navmesh_setGroupDestination",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 6 || !value::isNativeArray(args[0]))
                    return value::Value(static_cast<int64_t>(0));

                const auto& ids = value::asNativeArray(args[0]);
                if (!ids)
                    return value::Value(static_cast<int64_t>(0));

                events::navmesh::SetGroupDestinationCommand cmd;
                cmd.entities.reserve(ids->size());
                for (size_t i = 0; i < ids->size(); ++i)
                {
                    int64_t id = extractInt64((*ids)[i]);
                    if (id >= 0)
                        cmd.entities.push_back(intToEntity(id));
                }
                cmd.destination = glm::vec3(extractFloat(args[1]), extractFloat(args[2]),
                                            extractFloat(args[3]));
                cmd.spacing = extractFloat(args[4]);
                cmd.formationKind = static_cast<uint8_t>(extractInt64(args[5]));

                uint64_t groupId = dispatcher.execute(cmd);
                return value::Value(static_cast<int64_t>(groupId));
            }});

        interpreter->registerNativeFunction("_native_navmesh_stopAgent",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(std::monostate{});

                events::navmesh::StopAgentCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        // Toggle an entity's NavmeshObstacle on/off. Disable while previewing a
        // placement ghost so it does not re-bake tiles every frame; re-enable on
        // commit to carve the building's footprint into the navmesh once.
        interpreter->registerNativeFunction("_native_navmesh_setObstacleActive",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});

                events::navmesh::SetNavmeshObstacleActiveCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.active = extractBool(args[1]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_navmesh_isPointOnNavmesh",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 3) return value::Value(false);

                events::navmesh::IsPointOnNavmeshQuery query;
                query.point = glm::vec3(extractFloat(args[0]), extractFloat(args[1]),
                                         extractFloat(args[2]));
                return value::Value(dispatcher.query(query));
            }});

        // Monotonic tile version: bumped whenever streaming loads/unloads/rebakes a
        // tile. Cache it next to a findPath result and re-path when it changes.
        interpreter->registerNativeFunction("_native_navmesh_getTileVersion",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                uint64_t version = dispatcher.query(events::navmesh::GetNavmeshTileVersionQuery{});
                return value::Value(static_cast<int64_t>(version));
            }});

        interpreter->registerNativeFunction("_native_navmesh_isGroupArrived",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(false);

                events::navmesh::GetGroupStatusQuery query;
                query.groupId = static_cast<uint64_t>(extractInt64(args[0]));
                auto status = dispatcher.query(query);
                return value::Value(status.complete);
            }});

        interpreter->registerNativeFunction("_native_navmesh_getGroupCorridor",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty())
                {
                    auto empty = std::make_shared<value::NativeArray>(1, value::ValueType::FLOAT);
                    empty->set(0, value::Value(0.0f));
                    return value::Value(empty);
                }

                events::navmesh::GetGroupCorridorDebugQuery query;
                query.groupId = static_cast<uint64_t>(extractInt64(args[0]));
                auto points = dispatcher.query(query);

                auto result = std::make_shared<value::NativeArray>(
                    1 + static_cast<int>(points.size() * 3), value::ValueType::FLOAT);
                result->set(0, value::Value(static_cast<float>(points.size())));
                for (size_t i = 0; i < points.size(); ++i)
                {
                    result->set(static_cast<int>(1 + i * 3), value::Value(points[i].x));
                    result->set(static_cast<int>(2 + i * 3), value::Value(points[i].y));
                    result->set(static_cast<int>(3 + i * 3), value::Value(points[i].z));
                }
                return value::Value(result);
            }});

        interpreter->registerNativeFunction("_native_navmesh_getClosestPoint",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 3) return makeVec3Array(glm::vec3(0.0f));

                events::navmesh::GetClosestPointQuery query;
                query.point = glm::vec3(extractFloat(args[0]), extractFloat(args[1]),
                                         extractFloat(args[2]));
                return makeVec3Array(dispatcher.query(query));
            }});

        interpreter->registerNativeFunction("_native_navmesh_raycast",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (pathQueryCountThisFrame >= MAX_PATH_QUERIES_PER_FRAME)
                {
                    vfLogWarning("[Script] Navmesh query rate limit exceeded ({}/frame)",
                                 MAX_PATH_QUERIES_PER_FRAME);
                    auto result = std::make_shared<value::NativeArray>(4, value::ValueType::FLOAT);
                    result->set(0, value::Value(0.0f));
                    result->set(1, value::Value(0.0f));
                    result->set(2, value::Value(0.0f));
                    result->set(3, value::Value(0.0f));
                    return value::Value(result);
                }
                pathQueryCountThisFrame++;

                if (args.size() < 6)
                {
                    auto result = std::make_shared<value::NativeArray>(4, value::ValueType::FLOAT);
                    result->set(0, value::Value(0.0f));
                    result->set(1, value::Value(0.0f));
                    result->set(2, value::Value(0.0f));
                    result->set(3, value::Value(0.0f));
                    return value::Value(result);
                }

                events::navmesh::NavmeshRaycastQuery query;
                query.from = glm::vec3(extractFloat(args[0]), extractFloat(args[1]),
                                        extractFloat(args[2]));
                query.to = glm::vec3(extractFloat(args[3]), extractFloat(args[4]),
                                      extractFloat(args[5]));

                auto hit = dispatcher.query(query);

                auto result = std::make_shared<value::NativeArray>(4, value::ValueType::FLOAT);
                result->set(0, value::Value(hit.hit ? 1.0f : 0.0f));
                result->set(1, value::Value(hit.hitPoint.x));
                result->set(2, value::Value(hit.hitPoint.y));
                result->set(3, value::Value(hit.hitPoint.z));
                return value::Value(result);
            }});

        interpreter->registerNativeFunction("_native_navmesh_setAgentSpeed",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});

                events::navmesh::UpdateAgentConfigCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.maxSpeed = extractFloat(args[1]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_navmesh_setAgentAcceleration",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});

                events::navmesh::UpdateAgentConfigCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.maxAcceleration = extractFloat(args[1]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_navmesh_setRootMotionDriven",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});

                events::navmesh::UpdateAgentConfigCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.rootMotionDriven = extractBool(args[1]) ? 1 : 0;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_navmesh_setRootMotionScale",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});

                events::navmesh::UpdateAgentConfigCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.rootMotionSpeedScale = extractFloat(args[1]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_navmesh_setTurnSpeed",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});

                events::navmesh::UpdateAgentConfigCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.turnSpeed = extractFloat(args[1]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_navmesh_getAgentSpeed",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(0.0);

                events::navmesh::GetAgentSpeedQuery query;
                query.entity = intToEntity(extractInt64(args[0]));
                return value::Value(static_cast<double>(dispatcher.query(query)));
            }});

        interpreter->registerNativeFunction("_native_navmesh_getAgentVelocity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return makeVec3Array(glm::vec3(0.0f));

                events::navmesh::GetAgentVelocityQuery query;
                query.entity = intToEntity(extractInt64(args[0]));
                return makeVec3Array(dispatcher.query(query));
            }});

        registerVolumetricAPI(interpreter);
        registerEQSAPI(interpreter);

        vfLogInfo("[NavmeshAPI] Registered Navmesh native functions");
    }

    void NavmeshAPI::registerVolumetricAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        interpreter->registerNativeFunction("_native_volumetric_findPath3D",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (pathQueryCountThisFrame >= MAX_PATH_QUERIES_PER_FRAME)
                {
                    auto result = std::make_shared<value::NativeArray>(1, value::ValueType::FLOAT);
                    result->set(0, value::Value(0.0f));
                    return value::Value(result);
                }
                pathQueryCountThisFrame++;

                if (args.size() < 6)
                {
                    auto result = std::make_shared<value::NativeArray>(1, value::ValueType::FLOAT);
                    result->set(0, value::Value(0.0f));
                    return value::Value(result);
                }

                events::volumetric::FindPath3DQuery query;
                query.start = glm::vec3(extractFloat(args[0]), extractFloat(args[1]),
                                         extractFloat(args[2]));
                query.end = glm::vec3(extractFloat(args[3]), extractFloat(args[4]),
                                       extractFloat(args[5]));

                auto path = dispatcher.query(query);

                if (!path.isValid || path.waypoints.empty())
                {
                    auto result = std::make_shared<value::NativeArray>(1, value::ValueType::FLOAT);
                    result->set(0, value::Value(0.0f));
                    return value::Value(result);
                }

                size_t count = path.waypoints.size();
                auto result = std::make_shared<value::NativeArray>(
                    1 + static_cast<int>(count * 3), value::ValueType::FLOAT);
                result->set(0, value::Value(static_cast<float>(count)));
                for (size_t i = 0; i < count; ++i)
                {
                    result->set(static_cast<int>(1 + i * 3), value::Value(path.waypoints[i].x));
                    result->set(static_cast<int>(2 + i * 3), value::Value(path.waypoints[i].y));
                    result->set(static_cast<int>(3 + i * 3), value::Value(path.waypoints[i].z));
                }
                return value::Value(result);
            }});

        interpreter->registerNativeFunction("_native_volumetric_setDestination",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 4) return value::Value(std::monostate{});

                events::volumetric::SetVolumetricAgentDestinationCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.target = glm::vec3(extractFloat(args[1]), extractFloat(args[2]),
                                        extractFloat(args[3]));
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_volumetric_stopAgent",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(std::monostate{});

                events::volumetric::StopVolumetricAgentCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_volumetric_isPointNavigable",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 3) return value::Value(false);

                events::volumetric::IsPointNavigable3DQuery query;
                query.point = glm::vec3(extractFloat(args[0]), extractFloat(args[1]),
                                         extractFloat(args[2]));
                return value::Value(dispatcher.query(query));
            }});
    }

    void NavmeshAPI::registerEQSAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        interpreter->registerNativeFunction("_native_eqs_submitQuery",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 7) return value::Value(0.0);

                events::ai::SubmitEQSQueryCommand cmd;
                cmd.queryName = extractString(args[0]);
                cmd.context.querierEntityId = static_cast<uint64_t>(extractInt64(args[1]));
                cmd.context.querierPosition = glm::vec3(extractFloat(args[2]), extractFloat(args[3]),
                                                          extractFloat(args[4]));
                cmd.context.querierForward = glm::vec3(extractFloat(args[5]), 0.0f, extractFloat(args[6]));

                auto handle = dispatcher.execute(cmd);
                return value::Value(static_cast<double>(handle.id));
            }});

        interpreter->registerNativeFunction("_native_eqs_getResult",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return makeVec3Array(glm::vec3(0.0f));

                events::ai::GetEQSQueryResultQuery query;
                query.handle.id = static_cast<uint64_t>(extractInt64(args[0]));
                auto result = dispatcher.query(query);

                // Return: [status, bestX, bestY, bestZ, bestScore]
                auto arr = std::make_shared<value::NativeArray>(5, value::ValueType::FLOAT);
                arr->set(0, value::Value(static_cast<float>(static_cast<uint8_t>(result.status))));
                if (result.hasResults())
                {
                    arr->set(1, value::Value(result.getBestPosition().x));
                    arr->set(2, value::Value(result.getBestPosition().y));
                    arr->set(3, value::Value(result.getBestPosition().z));
                    arr->set(4, value::Value(result.getBestScore()));
                }
                return value::Value(arr);
            }});

        interpreter->registerNativeFunction("_native_eqs_cancelQuery",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(std::monostate{});

                events::ai::CancelEQSQueryCommand cmd;
                cmd.handle.id = static_cast<uint64_t>(extractInt64(args[0]));
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});
    }
}

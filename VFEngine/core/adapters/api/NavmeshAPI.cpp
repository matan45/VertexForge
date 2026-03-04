// mType headers must come first to avoid Windows macro conflicts
#include "print/Log.hpp"
#include <services/ScriptInterpreter.hpp>

#include "NavmeshAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/NavmeshEvents.hpp"

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
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
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
            });

        interpreter->registerNativeFunction("_native_navmesh_setDestination",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 4) return value::Value(std::monostate{});

                events::navmesh::SetAgentDestinationCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.target = glm::vec3(extractFloat(args[1]), extractFloat(args[2]),
                                        extractFloat(args[3]));
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        interpreter->registerNativeFunction("_native_navmesh_stopAgent",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(std::monostate{});

                events::navmesh::StopAgentCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        interpreter->registerNativeFunction("_native_navmesh_isPointOnNavmesh",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 3) return value::Value(false);

                events::navmesh::IsPointOnNavmeshQuery query;
                query.point = glm::vec3(extractFloat(args[0]), extractFloat(args[1]),
                                         extractFloat(args[2]));
                return value::Value(dispatcher.query(query));
            });

        interpreter->registerNativeFunction("_native_navmesh_getClosestPoint",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 3) return makeVec3Array(glm::vec3(0.0f));

                events::navmesh::GetClosestPointQuery query;
                query.point = glm::vec3(extractFloat(args[0]), extractFloat(args[1]),
                                         extractFloat(args[2]));
                return makeVec3Array(dispatcher.query(query));
            });

        vfLogInfo("[NavmeshAPI] Registered Navmesh native functions");
    }
}

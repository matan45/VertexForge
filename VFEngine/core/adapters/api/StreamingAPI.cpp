// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "StreamingAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/scene/StreamingZoneEvents.hpp"
#include "../../../services/events/scene/SceneManagementEvents.hpp"

namespace core::api
{
    void StreamingAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // _native_streaming_setTriggerZone(minX, minY, minZ, maxX, maxY, maxZ, scenePath) -> int (zone ID)
        interpreter->registerNativeFunction("_native_streaming_setTriggerZone",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 7)
                {
                    vfLogError("[Script] Streaming.setTriggerZone: requires 7 arguments (minX, minY, minZ, maxX, maxY, maxZ, scenePath)");
                    return value::Value(static_cast<int64_t>(0));
                }

                events::scene::SetStreamingTriggerZoneCommand cmd;
                cmd.boundsMin = glm::vec3(
                    extractFloat(args[0]),
                    extractFloat(args[1]),
                    extractFloat(args[2]));
                cmd.boundsMax = glm::vec3(
                    extractFloat(args[3]),
                    extractFloat(args[4]),
                    extractFloat(args[5]));
                cmd.scenePath = extractString(args[6], "Streaming.setTriggerZone");

                uint32_t zoneId = dispatcher.execute(cmd);
                return value::Value(static_cast<int64_t>(zoneId));
            }});

        // _native_streaming_removeTriggerZone(zoneId) -> void
        interpreter->registerNativeFunction("_native_streaming_removeTriggerZone",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty())
                {
                    vfLogError("[Script] Streaming.removeTriggerZone: missing zoneId argument");
                    return value::Value(std::monostate{});
                }

                events::scene::RemoveStreamingTriggerZoneCommand cmd;
                cmd.zoneId = static_cast<uint32_t>(extractInt64(args[0]));
                dispatcher.execute(cmd);

                return value::Value(std::monostate{});
            }});

        // _native_streaming_preload(scenePath) -> void
        interpreter->registerNativeFunction("_native_streaming_preload",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty())
                {
                    vfLogError("[Script] Streaming.preload: missing scenePath argument");
                    return value::Value(std::monostate{});
                }

                events::scene::PreloadSceneCommand cmd;
                cmd.scenePath = extractString(args[0], "Streaming.preload");
                dispatcher.execute(cmd);

                return value::Value(std::monostate{});
            }});

        // _native_streaming_isLoaded(scenePath) -> bool
        interpreter->registerNativeFunction("_native_streaming_isLoaded",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty())
                {
                    return value::Value(false);
                }

                std::string scenePath = extractString(args[0], "Streaming.isLoaded");

                events::scene::IsStreamingSceneLoadedQuery query;
                query.scenePath = scenePath;
                return value::Value(dispatcher.query(query));
            }});
    }
}

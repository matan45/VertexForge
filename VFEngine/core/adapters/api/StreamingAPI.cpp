// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "StreamingAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/scene/StreamingZoneEvents.hpp"
#include "../../../services/events/scene/SceneManagementEvents.hpp"
#include "../../../services/events/world/WorldSectorEvents.hpp"

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

        // ── World Sector streaming sources (gameplay-driven sector ring) ──

        // _native_streaming_registerWorldSource(x, y, z, radiusMultiplier, priority, ownerEntityUUID) -> int
        // Registers a gameplay streaming source (squad, command center, ...) that the
        // sector streamer rings around like a second camera. priority > 0 wins the
        // per-frame load budget over lower-priority sources. ownerEntityUUID != 0
        // auto-unregisters the source when that entity is deleted. Returns 0 on failure.
        interpreter->registerNativeFunction("_native_streaming_registerWorldSource",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 6)
                {
                    vfLogError("[Script] Streaming.registerWorldSource: requires 6 arguments (x, y, z, radiusMultiplier, priority, ownerEntityUUID)");
                    return value::Value(static_cast<int64_t>(0));
                }

                events::world::RegisterStreamingSourceCommand cmd;
                cmd.position = glm::vec3(extractFloat(args[0]), extractFloat(args[1]), extractFloat(args[2]));
                cmd.radiusMultiplier = extractFloat(args[3]);
                cmd.priority = static_cast<uint8_t>(extractInt64(args[4]));
                cmd.ownerEntityUUID = static_cast<uint64_t>(extractInt64(args[5]));
                try
                {
                    uint32_t id = events::EventDispatcher::instance().execute(cmd);
                    return value::Value(static_cast<int64_t>(id));
                }
                catch (const std::exception&)
                {
                    return value::Value(static_cast<int64_t>(0)); // no world service bound
                }
            }});

        // _native_streaming_registerWorldSourceEx(x, y, z, radiusMultiplier, priority,
        //                                         ownerEntityUUID, targetState) -> int
        // VK-1591. As registerWorldSource, plus a target-state cap:
        //   1 = prefetch only (the sector's bytes come into memory, NO entities spawn)
        //   2 = activate (identical to registerWorldSource)
        // Anything else clamps to 2 - target state 0 ("want nothing") is deliberately not
        // script-reachable, since a source that requests nothing is just an unregister.
        // A NEW native rather than a 7th argument on the existing one, so no shipped script's
        // arity changes.
        interpreter->registerNativeFunction("_native_streaming_registerWorldSourceEx",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 7)
                {
                    vfLogError("[Script] Streaming.registerWorldSourceEx: requires 7 arguments (x, y, z, radiusMultiplier, priority, ownerEntityUUID, targetState)");
                    return value::Value(static_cast<int64_t>(0));
                }

                events::world::RegisterStreamingSourceCommand cmd;
                cmd.position = glm::vec3(extractFloat(args[0]), extractFloat(args[1]), extractFloat(args[2]));
                cmd.radiusMultiplier = extractFloat(args[3]);
                cmd.priority = static_cast<uint8_t>(extractInt64(args[4]));
                cmd.ownerEntityUUID = static_cast<uint64_t>(extractInt64(args[5]));
                const int64_t targetState = extractInt64(args[6]);
                cmd.targetState = (targetState == 1) ? world::SectorTargetState::Prefetched
                                                     : world::SectorTargetState::Activated;
                try
                {
                    uint32_t id = events::EventDispatcher::instance().execute(cmd);
                    return value::Value(static_cast<int64_t>(id));
                }
                catch (const std::exception&)
                {
                    return value::Value(static_cast<int64_t>(0)); // no world service bound
                }
            }});

        // _native_streaming_unregisterWorldSource(sourceId) -> void
        interpreter->registerNativeFunction("_native_streaming_unregisterWorldSource",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (!args.empty())
                {
                    events::world::UnregisterStreamingSourceCommand cmd;
                    cmd.sourceId = static_cast<uint32_t>(extractInt64(args[0]));
                    try { events::EventDispatcher::instance().execute(cmd); }
                    catch (const std::exception&) {}
                }
                return value::Value(std::monostate{});
            }});

        // _native_streaming_updateWorldSource(sourceId, x, y, z) -> void
        interpreter->registerNativeFunction("_native_streaming_updateWorldSource",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() >= 4)
                {
                    events::world::UpdateStreamingSourcePositionCommand cmd;
                    cmd.sourceId = static_cast<uint32_t>(extractInt64(args[0]));
                    cmd.position = glm::vec3(extractFloat(args[1]), extractFloat(args[2]), extractFloat(args[3]));
                    try { events::EventDispatcher::instance().execute(cmd); }
                    catch (const std::exception&) {}
                }
                return value::Value(std::monostate{});
            }});

        // _native_streaming_isWorldSourceValid(sourceId) -> bool
        interpreter->registerNativeFunction("_native_streaming_isWorldSourceValid",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.empty())
                    return value::Value(false);

                events::world::IsStreamingSourceValidQuery query;
                query.sourceId = static_cast<uint32_t>(extractInt64(args[0]));
                try
                {
                    return value::Value(events::EventDispatcher::instance().query(query));
                }
                catch (const std::exception&)
                {
                    return value::Value(false);
                }
            }});

        // _native_streaming_isSectorLoadedAt(worldX, worldZ) -> bool
        // For gating AI activation / spawns on sector readiness around a world position
        interpreter->registerNativeFunction("_native_streaming_isSectorLoadedAt",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 2)
                    return value::Value(false);

                try
                {
                    auto& dispatcher = events::EventDispatcher::instance();

                    events::world::GetSectorAtPositionQuery posQuery;
                    posQuery.position = glm::vec3(extractFloat(args[0]), 0.0f, extractFloat(args[1]));
                    auto coordOpt = dispatcher.query(posQuery);
                    if (!coordOpt.has_value())
                        return value::Value(true); // not a sector world: everything is "loaded"

                    events::world::GetSectorStateQuery stateQuery;
                    stateQuery.coord = *coordOpt;
                    // VK-1591: deliberately Loaded ONLY. A Prefetched sector holds bytes and no
                    // entities, so a script gating AI activation or spawning on "is it loaded"
                    // must not be told yes.
                    return value::Value(dispatcher.query(stateQuery) == world::SectorState::Loaded);
                }
                catch (const std::exception&)
                {
                    return value::Value(true);
                }
            }});
    }
}

// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "OceanAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/terrain/OceanEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"

namespace core::api
{
    namespace
    {
        void registerQueryFunctions(services::ScriptInterpreter* interpreter,
                                    events::EventDispatcher& dispatcher)
        {
            // ocean.isInOcean(x, y, z) -> bool
            interpreter->registerNativeFunction("_native_ocean_isInOcean",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 3) return value::Value(false);

                    events::ocean::IsPositionInOceanQuery query;
                    query.position = glm::vec3(
                        extractFloat(args[0]),
                        extractFloat(args[1]),
                        extractFloat(args[2]));
                    return value::Value(dispatcher.query(query));
                }});

            // ocean.getOceanHeightAt(x, z) -> float
            interpreter->registerNativeFunction("_native_ocean_getOceanHeightAt",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 2) return value::Value(0.0f);

                    events::ocean::GetOceanHeightAtQuery query;
                    query.worldXZ = glm::vec2(
                        extractFloat(args[0]),
                        extractFloat(args[1]));
                    return value::Value(dispatcher.query(query));
                }});

            // VK-1605: ocean.getWaterDepthAt(x, z) -> float
            // Metres from the water surface down to the terrain. Returns a very large value where
            // there is no bottom, so "open ocean" needs no separate check.
            interpreter->registerNativeFunction("_native_ocean_getWaterDepthAt",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 2) return value::Value(0.0f);

                    events::ocean::GetWaterDepthAtQuery query;
                    query.worldXZ = glm::vec2(
                        extractFloat(args[0]),
                        extractFloat(args[1]));
                    return value::Value(dispatcher.query(query));
                }});

            // ocean.isCameraUnderwater() -> bool
            interpreter->registerNativeFunction("_native_ocean_isCameraUnderwater",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    auto& registry = scene::EntityRegistry::getRegistry();
                    auto view = registry.view<components::OceanComponent>();
                    for (auto entity : view)
                    {
                        // Check if the camera position (from CameraComponent) is below the
                        // displaced wave surface, not just the flat base height
                        auto camView = registry.view<components::CameraComponent, components::TransformComponent>();
                        for (auto camEntity : camView)
                        {
                            const auto& transform = camView.get<components::TransformComponent>(camEntity);
                            events::ocean::GetOceanHeightAtQuery query;
                            query.worldXZ = glm::vec2(transform.position.x, transform.position.z);
                            if (transform.position.y < dispatcher.query(query))
                                return value::Value(true);
                        }
                    }
                    return value::Value(false);
                }});

            // ocean.isEntityInWater(entityId) -> bool
            interpreter->registerNativeFunction("_native_ocean_isEntityInWater",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(false);
                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return value::Value(false);

                    events::ocean::IsEntityInWaterQuery query;
                    query.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                    return value::Value(dispatcher.query(query));
                }});

            // ocean.hasOcean(entityId) -> bool
            interpreter->registerNativeFunction("_native_ocean_hasOcean",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(false);
                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return value::Value(false);

                    events::ocean::HasOceanComponentQuery query;
                    query.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                    return value::Value(dispatcher.query(query));
                }});
        }

        void registerPropertyFunctions(services::ScriptInterpreter* interpreter)
        {
            // ocean.getBaseHeight(entityId) -> float
            interpreter->registerNativeFunction("_native_ocean_getBaseHeight",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    if (args.empty()) return value::Value(0.0f);
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(0.0f);

                    auto& registry = scene::EntityRegistry::getRegistry();
                    if (registry.all_of<components::OceanComponent>(*entity))
                    {
                        return value::Value(registry.get<components::OceanComponent>(*entity).waterHeight);
                    }
                    return value::Value(0.0f);
                }});

            // ocean.getDensity(entityId) -> float
            interpreter->registerNativeFunction("_native_ocean_getDensity",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    if (args.empty()) return value::Value(1000.0f);
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(1000.0f);

                    auto& registry = scene::EntityRegistry::getRegistry();
                    if (!registry.all_of<components::OceanComponent>(*entity))
                        return value::Value(1000.0f);

                    return value::Value(registry.get<components::OceanComponent>(*entity).density);
                }});

            // ocean.getDrag(entityId) -> float
            interpreter->registerNativeFunction("_native_ocean_getDrag",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    if (args.empty()) return value::Value(0.5f);
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(0.5f);

                    auto& registry = scene::EntityRegistry::getRegistry();
                    if (!registry.all_of<components::OceanComponent>(*entity))
                        return value::Value(0.5f);

                    return value::Value(registry.get<components::OceanComponent>(*entity).drag);
                }});

            // ocean.getBuoyancyStrength(entityId) -> float
            interpreter->registerNativeFunction("_native_ocean_getBuoyancyStrength",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    if (args.empty()) return value::Value(2.0f);
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(2.0f);

                    auto& registry = scene::EntityRegistry::getRegistry();
                    if (!registry.all_of<components::OceanComponent>(*entity))
                        return value::Value(2.0f);

                    return value::Value(registry.get<components::OceanComponent>(*entity).buoyancyStrength);
                }});
        }

        void registerCommandFunctions(services::ScriptInterpreter* interpreter,
                                      events::EventDispatcher& dispatcher)
        {
            // ocean.setPhysicsSettings(oceanEntityId, density, drag, buoyancyStrength)
            interpreter->registerNativeFunction("_native_ocean_setPhysicsSettings",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 4) return value::Value(std::monostate{});
                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return value::Value(std::monostate{});

                    events::ocean::SetOceanPhysicsSettingsCommand cmd;
                    cmd.oceanEntity = services::EntityHandle{static_cast<uint64_t>(id)};
                    cmd.settings.density = extractFloat(args[1]);
                    cmd.settings.drag = extractFloat(args[2]);
                    cmd.settings.buoyancyStrength = extractFloat(args[3]);
                    dispatcher.execute(cmd);

                    return value::Value(std::monostate{});
                }});

            // ocean.setSeaState(beaufort, transitionSeconds)
            interpreter->registerNativeFunction("_native_ocean_setSeaState",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(std::monostate{});

                    events::ocean::SetOceanSeaStateCommand cmd;
                    cmd.beaufort = extractFloat(args[0]);
                    cmd.transitionSeconds = args.size() > 1 ? extractFloat(args[1]) : 0.0f;
                    dispatcher.execute(cmd);

                    return value::Value(std::monostate{});
                }});

            // ocean.getSeaState() -> float (current Beaufort number)
            interpreter->registerNativeFunction("_native_ocean_getSeaState",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    events::ocean::GetOceanSeaStateQuery query;
                    return value::Value(dispatcher.query(query));
                }});

            // ocean.setWeatherDriven(oceanEntityId, enabled)
            interpreter->registerNativeFunction("_native_ocean_setWeatherDriven",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 2) return value::Value(std::monostate{});
                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return value::Value(std::monostate{});

                    events::ocean::SetOceanWeatherDrivenCommand cmd;
                    cmd.oceanEntity = services::EntityHandle{static_cast<uint64_t>(id)};
                    cmd.enabled = extractBool(args[1]);
                    cmd.response = args.size() > 2 ? extractFloat(args[2]) : 1.0f;
                    dispatcher.execute(cmd);

                    return value::Value(std::monostate{});
                }});

            // ocean.setVisualSettings(oceanEntityId, shallowR, shallowG, shallowB, shallowA,
            //                         deepR, deepG, deepB, deepA, maxVisibleDepth, fresnelPower)
            interpreter->registerNativeFunction("_native_ocean_setVisualSettings",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 11) return value::Value(std::monostate{});
                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return value::Value(std::monostate{});

                    events::ocean::SetOceanVisualSettingsCommand cmd;
                    cmd.oceanEntity = services::EntityHandle{static_cast<uint64_t>(id)};

                    // VK-1604: read-modify-write. The service handler writes EVERY field of the
                    // struct, so sending a default-constructed one would reset refraction,
                    // caustics, shore, SSR, absorption and anti-tiling to their defaults. Seed
                    // from the live settings and overwrite only what the script passed.
                    events::ocean::GetOceanVisualSettingsQuery settingsQuery;
                    settingsQuery.entity = cmd.oceanEntity;
                    if (auto current = dispatcher.query(settingsQuery); current.has_value())
                        cmd.settings = *current;

                    cmd.settings.shallowColor = glm::vec4(
                        extractFloat(args[1]), extractFloat(args[2]),
                        extractFloat(args[3]), extractFloat(args[4]));
                    cmd.settings.deepColor = glm::vec4(
                        extractFloat(args[5]), extractFloat(args[6]),
                        extractFloat(args[7]), extractFloat(args[8]));
                    cmd.settings.maxVisibleDepth = extractFloat(args[9]);
                    cmd.settings.fresnelPower = extractFloat(args[10]);
                    dispatcher.execute(cmd);

                    return value::Value(std::monostate{});
                }});
        }
    }

    void OceanAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        registerQueryFunctions(interpreter, dispatcher);
        registerPropertyFunctions(interpreter);
        registerCommandFunctions(interpreter, dispatcher);
    }
}

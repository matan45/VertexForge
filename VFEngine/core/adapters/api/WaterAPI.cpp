// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "WaterAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/terrain/WaterEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"

namespace core::api
{
    namespace
    {
        void registerQueryFunctions(services::ScriptInterpreter* interpreter,
                                    events::EventDispatcher& dispatcher)
        {
            // water.isInWater(x, y, z) -> bool
            interpreter->registerNativeFunction("_native_water_isInWater",
                [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.size() < 3) return value::Value(false);

                    events::water::IsPositionInWaterQuery query;
                    query.position = glm::vec3(
                        extractFloat(args[0]),
                        extractFloat(args[1]),
                        extractFloat(args[2]));
                    return value::Value(dispatcher.query(query));
                });

            // water.getHeightAt(x, z) -> float
            interpreter->registerNativeFunction("_native_water_getHeightAt",
                [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.size() < 2) return value::Value(0.0f);

                    events::water::GetWaterHeightAtQuery query;
                    query.worldXZ = glm::vec2(
                        extractFloat(args[0]),
                        extractFloat(args[1]));
                    return value::Value(dispatcher.query(query));
                });

            // water.hasWater(entityId) -> bool
            interpreter->registerNativeFunction("_native_water_hasWater",
                [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.empty()) return value::Value(false);
                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return value::Value(false);

                    events::water::HasWaterComponentQuery query;
                    query.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                    return value::Value(dispatcher.query(query));
                });

            // water.hasWaterTile(entityId) -> bool
            interpreter->registerNativeFunction("_native_water_hasWaterTile",
                [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.empty()) return value::Value(false);
                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return value::Value(false);

                    events::water::HasWaterTileComponentQuery query;
                    query.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                    return value::Value(dispatcher.query(query));
                });
        }

        void registerPropertyFunctions(services::ScriptInterpreter* interpreter)
        {
            // water.getWaterHeight(entityId) -> float
            interpreter->registerNativeFunction("_native_water_getWaterHeight",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.empty()) return value::Value(0.0f);
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(0.0f);

                    auto& registry = scene::EntityRegistry::getRegistry();
                    if (registry.all_of<components::WaterComponent>(*entity))
                    {
                        return value::Value(registry.get<components::WaterComponent>(*entity).defaultWaterHeight);
                    }
                    if (registry.all_of<components::WaterTileComponent>(*entity))
                    {
                        return value::Value(registry.get<components::WaterTileComponent>(*entity).waterHeight);
                    }
                    return value::Value(0.0f);
                });

            // water.getWaveSpeed(entityId) -> float
            interpreter->registerNativeFunction("_native_water_getWaveSpeed",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.empty()) return value::Value(1.0f);
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(1.0f);

                    auto& registry = scene::EntityRegistry::getRegistry();
                    if (!registry.all_of<components::WaterComponent>(*entity))
                        return value::Value(1.0f);

                    return value::Value(registry.get<components::WaterComponent>(*entity).waveSpeed);
                });

            // water.getWaveAmplitude(entityId) -> float
            interpreter->registerNativeFunction("_native_water_getWaveAmplitude",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.empty()) return value::Value(0.5f);
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(0.5f);

                    auto& registry = scene::EntityRegistry::getRegistry();
                    if (!registry.all_of<components::WaterComponent>(*entity))
                        return value::Value(0.5f);

                    return value::Value(registry.get<components::WaterComponent>(*entity).waveAmplitude);
                });

            // water.getDensity(entityId) -> float
            interpreter->registerNativeFunction("_native_water_getDensity",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.empty()) return value::Value(1000.0f);
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(1000.0f);

                    auto& registry = scene::EntityRegistry::getRegistry();
                    if (!registry.all_of<components::WaterComponent>(*entity))
                        return value::Value(1000.0f);

                    return value::Value(registry.get<components::WaterComponent>(*entity).globalDensity);
                });

            // water.getDrag(entityId) -> float
            interpreter->registerNativeFunction("_native_water_getDrag",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.empty()) return value::Value(0.5f);
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(0.5f);

                    auto& registry = scene::EntityRegistry::getRegistry();
                    if (!registry.all_of<components::WaterComponent>(*entity))
                        return value::Value(0.5f);

                    return value::Value(registry.get<components::WaterComponent>(*entity).globalDrag);
                });

            // water.getBuoyancyStrength(entityId) -> float
            interpreter->registerNativeFunction("_native_water_getBuoyancyStrength",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.empty()) return value::Value(2.0f);
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(2.0f);

                    auto& registry = scene::EntityRegistry::getRegistry();
                    if (!registry.all_of<components::WaterComponent>(*entity))
                        return value::Value(2.0f);

                    return value::Value(registry.get<components::WaterComponent>(*entity).globalBuoyancyStrength);
                });
        }

        void registerCommandFunctions(services::ScriptInterpreter* interpreter,
                                      events::EventDispatcher& dispatcher)
        {
            // water.setTileHeight(waterEntityId, tileX, tileZ, height)
            interpreter->registerNativeFunction("_native_water_setTileHeight",
                [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.size() < 4) return value::Value(std::monostate{});
                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return value::Value(std::monostate{});

                    events::water::SetWaterTileHeightCommand cmd;
                    cmd.waterEntity = services::EntityHandle{static_cast<uint64_t>(id)};
                    cmd.tileX = static_cast<int32_t>(extractInt64(args[1]));
                    cmd.tileZ = static_cast<int32_t>(extractInt64(args[2]));
                    cmd.waterHeight = extractFloat(args[3]);
                    dispatcher.execute(cmd);

                    return value::Value(std::monostate{});
                });

            // water.setGlobalSettings(waterEntityId, density, drag, buoyancyStrength,
            //                         waveSpeed, waveAmplitude, waveFrequency)
            interpreter->registerNativeFunction("_native_water_setGlobalSettings",
                [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.size() < 7) return value::Value(std::monostate{});
                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return value::Value(std::monostate{});

                    events::water::SetWaterGlobalSettingsCommand cmd;
                    cmd.waterEntity = services::EntityHandle{static_cast<uint64_t>(id)};
                    cmd.settings.density = extractFloat(args[1]);
                    cmd.settings.drag = extractFloat(args[2]);
                    cmd.settings.buoyancyStrength = extractFloat(args[3]);
                    cmd.settings.waveSpeed = extractFloat(args[4]);
                    cmd.settings.waveAmplitude = extractFloat(args[5]);
                    cmd.settings.waveFrequency = extractFloat(args[6]);
                    dispatcher.execute(cmd);

                    return value::Value(std::monostate{});
                });
        }
    }

    void WaterAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        registerQueryFunctions(interpreter, dispatcher);
        registerPropertyFunctions(interpreter);
        registerCommandFunctions(interpreter, dispatcher);
    }
}

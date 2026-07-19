// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>

#include "AtmosphereAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/render/AtmosphereEvents.hpp"

#include <span>

namespace core::api
{
    namespace
    {
        // Note: each setter does a full query+apply cycle. For batch updates,
        // scripts should modify multiple properties then call a single apply.
        // This matches the pattern used by PostProcessAPI and other engine APIs.
        template<typename Mutator>
        value::Value modifyAtmosphere(events::EventDispatcher& dispatcher, Mutator&& mutator)
        {
            auto settings = dispatcher.query(events::atmosphere::GetAtmosphereSettingsQuery{});
            mutator(settings);
            events::atmosphere::ApplyAtmosphereSettingsCommand cmd;
            cmd.settings = settings;
            dispatcher.execute(cmd);
            return value::Value(std::monostate{});
        }
    }

    void AtmosphereAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        // ── Enable / Disable ──

        interpreter->registerNativeFunction("_native_atmosphere_isEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value
            {
                auto& dispatcher = events::EventDispatcher::instance();
                return value::Value(dispatcher.query(events::atmosphere::GetAtmosphereEnabledQuery{}));
            }});

        interpreter->registerNativeFunction("_native_atmosphere_setEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value
            {
                auto& dispatcher = events::EventDispatcher::instance();
                events::atmosphere::SetAtmosphereEnabledCommand cmd;
                cmd.enabled = extractBool(args[0]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        // ── Planet ──

        interpreter->registerNativeFunction("_native_atmosphere_setPlanetRadius",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value
            {
                return modifyAtmosphere(events::EventDispatcher::instance(), [&](render::atmosphere::AtmosphereSettings& s)
                { s.planetRadius = extractFloat(args[0]); });
            }});

        interpreter->registerNativeFunction("_native_atmosphere_setAtmosphereRadius",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value
            {
                return modifyAtmosphere(events::EventDispatcher::instance(), [&](render::atmosphere::AtmosphereSettings& s)
                { s.atmosphereRadius = extractFloat(args[0]); });
            }});

        // ── Rayleigh ──

        interpreter->registerNativeFunction("_native_atmosphere_setRayleighScattering",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value
            {
                return modifyAtmosphere(events::EventDispatcher::instance(), [&](render::atmosphere::AtmosphereSettings& s)
                { s.rayleighScattering = glm::vec3(extractFloat(args[0]), extractFloat(args[1]), extractFloat(args[2])); });
            }});

        // ── Mie ──

        interpreter->registerNativeFunction("_native_atmosphere_setMieScattering",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value
            {
                return modifyAtmosphere(events::EventDispatcher::instance(), [&](render::atmosphere::AtmosphereSettings& s)
                { s.mieScattering = extractFloat(args[0]); });
            }});

        interpreter->registerNativeFunction("_native_atmosphere_setMieAnisotropy",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value
            {
                return modifyAtmosphere(events::EventDispatcher::instance(), [&](render::atmosphere::AtmosphereSettings& s)
                { s.mieAnisotropy = extractFloat(args[0]); });
            }});

        // ── Sun ──

        interpreter->registerNativeFunction("_native_atmosphere_setSunIrradiance",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value
            {
                return modifyAtmosphere(events::EventDispatcher::instance(), [&](render::atmosphere::AtmosphereSettings& s)
                { s.sunIrradiance = glm::vec3(extractFloat(args[0]), extractFloat(args[1]), extractFloat(args[2])); });
            }});

        interpreter->registerNativeFunction("_native_atmosphere_getSunIrradiance",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value
            {
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::atmosphere::GetAtmosphereSettingsQuery{});
                return makeVec3Array(s.sunIrradiance);
            }});

        interpreter->registerNativeFunction("_native_atmosphere_setSunElevation",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value
            {
                return modifyAtmosphere(events::EventDispatcher::instance(), [&](render::atmosphere::AtmosphereSettings& s)
                { s.sunElevation = extractFloat(args[0]); });
            }});

        interpreter->registerNativeFunction("_native_atmosphere_setSunAzimuth",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value
            {
                return modifyAtmosphere(events::EventDispatcher::instance(), [&](render::atmosphere::AtmosphereSettings& s)
                { s.sunAzimuth = extractFloat(args[0]); });
            }});

        // ── Ground ──

        interpreter->registerNativeFunction("_native_atmosphere_setGroundAlbedo",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value
            {
                return modifyAtmosphere(events::EventDispatcher::instance(), [&](render::atmosphere::AtmosphereSettings& s)
                { s.groundAlbedo = glm::vec3(extractFloat(args[0]), extractFloat(args[1]), extractFloat(args[2])); });
            }});

        // ── Aerial Perspective ──

        interpreter->registerNativeFunction("_native_atmosphere_setAerialMaxDist",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value
            {
                return modifyAtmosphere(events::EventDispatcher::instance(), [&](render::atmosphere::AtmosphereSettings& s)
                { s.aerialMaxDist = extractFloat(args[0]); });
            }});

        interpreter->registerNativeFunction("_native_atmosphere_setAerialIntensity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value
            {
                return modifyAtmosphere(events::EventDispatcher::instance(), [&](render::atmosphere::AtmosphereSettings& s)
                { s.aerialIntensity = extractFloat(args[0]); });
            }});

        // ── Day-Night Cycle ──

        interpreter->registerNativeFunction("_native_atmosphere_isDayNightEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value
            {
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::atmosphere::GetAtmosphereSettingsQuery{});
                return value::Value(s.dayNightEnabled);
            }});

        interpreter->registerNativeFunction("_native_atmosphere_setDayNightEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value
            {
                return modifyAtmosphere(events::EventDispatcher::instance(), [&](render::atmosphere::AtmosphereSettings& s)
                { s.dayNightEnabled = extractBool(args[0]); });
            }});

        interpreter->registerNativeFunction("_native_atmosphere_getTimeOfDay",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value
            {
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::atmosphere::GetAtmosphereSettingsQuery{});
                return value::Value(static_cast<double>(s.timeOfDay));
            }});

        interpreter->registerNativeFunction("_native_atmosphere_setTimeOfDay",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value
            {
                return modifyAtmosphere(events::EventDispatcher::instance(), [&](render::atmosphere::AtmosphereSettings& s)
                { s.timeOfDay = extractFloat(args[0]); });
            }});

        interpreter->registerNativeFunction("_native_atmosphere_getCycleSpeed",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value
            {
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::atmosphere::GetAtmosphereSettingsQuery{});
                return value::Value(static_cast<double>(s.cycleSpeed));
            }});

        interpreter->registerNativeFunction("_native_atmosphere_setCycleSpeed",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value
            {
                return modifyAtmosphere(events::EventDispatcher::instance(), [&](render::atmosphere::AtmosphereSettings& s)
                { s.cycleSpeed = extractFloat(args[0]); });
            }});

        // ── Sun -> scene light feedback (VK-1566) ──

        interpreter->registerNativeFunction("_native_atmosphere_isSunColorFromAtmosphere",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value
            {
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::atmosphere::GetAtmosphereSettingsQuery{});
                return value::Value(s.sunColorFromAtmosphere);
            }});

        interpreter->registerNativeFunction("_native_atmosphere_setSunColorFromAtmosphere",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value
            {
                return modifyAtmosphere(events::EventDispatcher::instance(), [&](render::atmosphere::AtmosphereSettings& s)
                { s.sunColorFromAtmosphere = extractBool(args[0]); });
            }});

        interpreter->registerNativeFunction("_native_atmosphere_getSunColorFeedbackStrength",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value
            {
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::atmosphere::GetAtmosphereSettingsQuery{});
                return value::Value(static_cast<double>(s.sunColorFeedbackStrength));
            }});

        interpreter->registerNativeFunction("_native_atmosphere_setSunColorFeedbackStrength",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value
            {
                return modifyAtmosphere(events::EventDispatcher::instance(), [&](render::atmosphere::AtmosphereSettings& s)
                { s.sunColorFeedbackStrength = extractFloat(args[0]); });
            }});

        interpreter->registerNativeFunction("_native_atmosphere_isCycleControlsSunEntity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value
            {
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::atmosphere::GetAtmosphereSettingsQuery{});
                return value::Value(s.cycleControlsSunEntity);
            }});

        interpreter->registerNativeFunction("_native_atmosphere_setCycleControlsSunEntity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value
            {
                return modifyAtmosphere(events::EventDispatcher::instance(), [&](render::atmosphere::AtmosphereSettings& s)
                { s.cycleControlsSunEntity = extractBool(args[0]); });
            }});

        // ── Dynamic sky -> IBL ambient (VK-1569) ──

        interpreter->registerNativeFunction("_native_atmosphere_isDynamicAmbient",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value
            {
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::atmosphere::GetAtmosphereSettingsQuery{});
                return value::Value(s.dynamicAmbient);
            }});

        interpreter->registerNativeFunction("_native_atmosphere_setDynamicAmbient",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value
            {
                return modifyAtmosphere(events::EventDispatcher::instance(), [&](render::atmosphere::AtmosphereSettings& s)
                { s.dynamicAmbient = extractBool(args[0]); });
            }});

        interpreter->registerNativeFunction("_native_atmosphere_getAmbientIntensity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value
            {
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::atmosphere::GetAtmosphereSettingsQuery{});
                return value::Value(static_cast<double>(s.ambientIntensity));
            }});

        interpreter->registerNativeFunction("_native_atmosphere_setAmbientIntensity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value
            {
                return modifyAtmosphere(events::EventDispatcher::instance(), [&](render::atmosphere::AtmosphereSettings& s)
                { s.ambientIntensity = extractFloat(args[0]); });
            }});

        interpreter->registerNativeFunction("_native_atmosphere_getAmbientItemsPerFrame",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value
            {
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::atmosphere::GetAtmosphereSettingsQuery{});
                return value::Value(static_cast<int64_t>(s.ambientItemsPerFrame));
            }});

        interpreter->registerNativeFunction("_native_atmosphere_setAmbientItemsPerFrame",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value
            {
                return modifyAtmosphere(events::EventDispatcher::instance(), [&](render::atmosphere::AtmosphereSettings& s)
                { s.ambientItemsPerFrame = static_cast<int>(extractInt64(args[0])); });
            }});

        // ── Moon ──

        interpreter->registerNativeFunction("_native_atmosphere_getMoonBrightness",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value
            {
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::atmosphere::GetAtmosphereSettingsQuery{});
                return value::Value(static_cast<double>(s.moonBrightness));
            }});

        interpreter->registerNativeFunction("_native_atmosphere_setMoonBrightness",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value
            {
                return modifyAtmosphere(events::EventDispatcher::instance(), [&](render::atmosphere::AtmosphereSettings& s)
                { s.moonBrightness = extractFloat(args[0]); });
            }});

        // ── Stars ──

        interpreter->registerNativeFunction("_native_atmosphere_getStarDensity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value
            {
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::atmosphere::GetAtmosphereSettingsQuery{});
                return value::Value(static_cast<double>(s.starDensity));
            }});

        interpreter->registerNativeFunction("_native_atmosphere_setStarDensity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value
            {
                return modifyAtmosphere(events::EventDispatcher::instance(), [&](render::atmosphere::AtmosphereSettings& s)
                { s.starDensity = extractFloat(args[0]); });
            }});

        interpreter->registerNativeFunction("_native_atmosphere_getStarBrightness",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value
            {
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::atmosphere::GetAtmosphereSettingsQuery{});
                return value::Value(static_cast<double>(s.starBrightness));
            }});

        interpreter->registerNativeFunction("_native_atmosphere_setStarBrightness",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value
            {
                return modifyAtmosphere(events::EventDispatcher::instance(), [&](render::atmosphere::AtmosphereSettings& s)
                { s.starBrightness = extractFloat(args[0]); });
            }});

        interpreter->registerNativeFunction("_native_atmosphere_getNightSkyBrightness",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value
            {
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::atmosphere::GetAtmosphereSettingsQuery{});
                return value::Value(static_cast<double>(s.nightSkyBrightness));
            }});

        interpreter->registerNativeFunction("_native_atmosphere_setNightSkyBrightness",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value
            {
                return modifyAtmosphere(events::EventDispatcher::instance(), [&](render::atmosphere::AtmosphereSettings& s)
                { s.nightSkyBrightness = extractFloat(args[0]); });
            }});
    }
}

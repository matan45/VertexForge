// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "AtmosphereAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/render/AtmosphereEvents.hpp"

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
        auto& dispatcher = events::EventDispatcher::instance();

        // ── Enable / Disable ──

        interpreter->registerNativeFunction("_native_atmosphere_isEnabled",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                return value::Value(dispatcher.query(events::atmosphere::GetAtmosphereEnabledQuery{}));
            });

        interpreter->registerNativeFunction("_native_atmosphere_setEnabled",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                events::atmosphere::SetAtmosphereEnabledCommand cmd;
                cmd.enabled = extractBool(args[0]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // ── Planet ──

        interpreter->registerNativeFunction("_native_atmosphere_setPlanetRadius",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifyAtmosphere(dispatcher, [&](render::atmosphere::AtmosphereSettings& s)
                { s.planetRadius = extractFloat(args[0]); });
            });

        interpreter->registerNativeFunction("_native_atmosphere_setAtmosphereRadius",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifyAtmosphere(dispatcher, [&](render::atmosphere::AtmosphereSettings& s)
                { s.atmosphereRadius = extractFloat(args[0]); });
            });

        // ── Rayleigh ──

        interpreter->registerNativeFunction("_native_atmosphere_setRayleighScattering",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifyAtmosphere(dispatcher, [&](render::atmosphere::AtmosphereSettings& s)
                { s.rayleighScattering = glm::vec3(extractFloat(args[0]), extractFloat(args[1]), extractFloat(args[2])); });
            });

        // ── Mie ──

        interpreter->registerNativeFunction("_native_atmosphere_setMieScattering",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifyAtmosphere(dispatcher, [&](render::atmosphere::AtmosphereSettings& s)
                { s.mieScattering = extractFloat(args[0]); });
            });

        interpreter->registerNativeFunction("_native_atmosphere_setMieAnisotropy",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifyAtmosphere(dispatcher, [&](render::atmosphere::AtmosphereSettings& s)
                { s.mieAnisotropy = extractFloat(args[0]); });
            });

        // ── Sun ──

        interpreter->registerNativeFunction("_native_atmosphere_setSunIrradiance",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifyAtmosphere(dispatcher, [&](render::atmosphere::AtmosphereSettings& s)
                { s.sunIrradiance = glm::vec3(extractFloat(args[0]), extractFloat(args[1]), extractFloat(args[2])); });
            });

        interpreter->registerNativeFunction("_native_atmosphere_getSunIrradiance",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::atmosphere::GetAtmosphereSettingsQuery{});
                return makeVec3Array(s.sunIrradiance);
            });

        interpreter->registerNativeFunction("_native_atmosphere_setSunElevation",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifyAtmosphere(dispatcher, [&](render::atmosphere::AtmosphereSettings& s)
                { s.sunElevation = extractFloat(args[0]); });
            });

        interpreter->registerNativeFunction("_native_atmosphere_setSunAzimuth",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifyAtmosphere(dispatcher, [&](render::atmosphere::AtmosphereSettings& s)
                { s.sunAzimuth = extractFloat(args[0]); });
            });

        // ── Ground ──

        interpreter->registerNativeFunction("_native_atmosphere_setGroundAlbedo",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifyAtmosphere(dispatcher, [&](render::atmosphere::AtmosphereSettings& s)
                { s.groundAlbedo = glm::vec3(extractFloat(args[0]), extractFloat(args[1]), extractFloat(args[2])); });
            });

        // ── Aerial Perspective ──

        interpreter->registerNativeFunction("_native_atmosphere_setAerialMaxDist",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifyAtmosphere(dispatcher, [&](render::atmosphere::AtmosphereSettings& s)
                { s.aerialMaxDist = extractFloat(args[0]); });
            });

        interpreter->registerNativeFunction("_native_atmosphere_setAerialIntensity",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifyAtmosphere(dispatcher, [&](render::atmosphere::AtmosphereSettings& s)
                { s.aerialIntensity = extractFloat(args[0]); });
            });
    }
}

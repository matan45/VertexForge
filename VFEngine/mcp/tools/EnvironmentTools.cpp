#include "CoreTools.hpp"
#include "EnvironmentToolSupport.hpp"
#include "PathSandbox.hpp"
#include "TerrainToolSupport.hpp"
#include "ToolHelpers.hpp"
#include "../protocol/ArgReader.hpp"
#include "../undo/EnvironmentUndo.hpp"
#include "../undo/McpUndo.hpp"
#include "../undo/McpUndoCommands.hpp"
#include "../../graphics/render/gi/GIRebuildPolicy.hpp"

#include "asset/AssetRef.hpp"
#include "atmosphere/DayNightCycleController.hpp"
#include "atmosphere/SunEntitySync.hpp"
#include "events/EventDispatcher.hpp"
#include "events/render/AtmosphereEvents.hpp"
#include "events/render/CloudEvents.hpp"
#include "events/render/GIEvents.hpp"
#include "events/render/PostProcessEvents.hpp"
#include "events/render/RenderEvents.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/scene/ReflectionProbeEvents.hpp"
#include "events/weather/WeatherEvents.hpp"
#include "weather/WeatherPresets.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <memory>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

// VK-1654 environment & lighting tools: sky/atmosphere + time of day + sun, image-based lighting,
// global illumination, volumetric fog and the session weather. Every *_set tool refuses in Play mode
// and while a scene load is pending, validates the WHOLE request before touching the engine, writes
// each settings section through env::writeSections (scene copy + live copy + notification) and pushes
// exactly one undo batch - none when nothing changes. A failure part-way rolls the batch back.
namespace mcp::tools
{
    namespace
    {
        constexpr std::chrono::milliseconds environmentTimeout{30000};
        constexpr std::chrono::milliseconds iblTimeout{120000};
        constexpr std::chrono::milliseconds giTimeout{60000};
        constexpr std::chrono::milliseconds fogTimeout{30000};
        constexpr std::chrono::milliseconds weatherTimeout{30000};

        constexpr double maxSunIntensity = 100.0;
        constexpr double maxIblIntensity = 5.0;
        constexpr double maxAmbientIntensity = 10.0;     // AtmosphereSettings::ambientIntensity (loader clamp)
        constexpr double maxTransitionSeconds = 120.0;
        constexpr double sunSyncToleranceDeg = 0.5;      // environment_get sun.inSync
        constexpr float presetTolerance = 1.0e-4f;       // weather_get matchesPreset
        constexpr double lnTwenty = 2.995732273553991;   // visibility = where 95% of the light is gone
        constexpr double raymarchExtinctionCutoff = 1.0e-4; // volumetric_raymarch.glsl skips a lower extinction

        const std::vector<std::string> iblSources{"hdr", "dynamicSky", "none"};
        const std::vector<std::string> bakeTargets{"environment", "probes", "all"};

        constexpr std::array<::weather::WeatherPresetId, 10> weatherPresets{
            ::weather::WeatherPresetId::Clear, ::weather::WeatherPresetId::Cloudy,
            ::weather::WeatherPresetId::Overcast, ::weather::WeatherPresetId::LightRain,
            ::weather::WeatherPresetId::HeavyRain, ::weather::WeatherPresetId::Thunderstorm,
            ::weather::WeatherPresetId::LightSnow, ::weather::WeatherPresetId::HeavySnow,
            ::weather::WeatherPresetId::Fog, ::weather::WeatherPresetId::Sandstorm};

        const std::vector<std::string>& giPresetNames()
        {
            static const std::vector<std::string> names(env::giQualityNames.begin(), env::giQualityNames.end());
            return names;
        }

        const std::vector<std::string>& weatherPresetNames()
        {
            static const std::vector<std::string> names = []
            {
                std::vector<std::string> list;
                for (::weather::WeatherPresetId id : weatherPresets)
                {
                    list.emplace_back(::weather::presetIdToString(id));
                }
                return list;
            }();
            return names;
        }

        std::string joinedText(const std::vector<std::string>& items)
        {
            std::string out;
            for (const std::string& item : items)
            {
                out += out.empty() ? item : ", " + item;
            }
            return out;
        }

        // ------------------------------------------------------------------
        // Results
        // ------------------------------------------------------------------

        // One section's undo entry, then its write: recorded first, a rollback also restores a write that
        // failed half-way.
        void writeRecorded(undo::UndoRecorder& recorder, env::SectionValue before, env::SectionValue after)
        {
            const std::vector<env::SectionValue> written{std::move(after)};
            recorder.add(std::make_unique<undo::RenderSettingsSectionUndo>(
                std::vector<env::SectionValue>{std::move(before)}, written));
            env::writeSections(written);
        }

        nlohmann::json rgbSchema(std::string description)
        {
            nlohmann::json out = schema::vec3(std::move(description));
            out["items"] = {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0}};
            return out;
        }

        std::string numberText(double value)
        {
            return std::format("{:g}", value);
        }

        std::string entityText(const services::EntityHandle& handle, const std::string& name)
        {
            return "'" + name + "' (id " + std::to_string(toId(handle)) + ")";
        }

        // ------------------------------------------------------------------
        // Engine reads
        // ------------------------------------------------------------------

        types::RenderSettings authoredSettings()
        {
            return events::EventDispatcher::instance().query(events::scene::GetRenderSettingsQuery{});
        }

        env::AtmosphereSettings liveAtmosphere()
        {
            return events::EventDispatcher::instance().query(events::atmosphere::GetAtmosphereSettingsQuery{});
        }

        env::CloudSettings liveClouds()
        {
            return events::EventDispatcher::instance().query(events::cloud::GetCloudSettingsQuery{});
        }

        env::FogSettings liveFog()
        {
            return events::EventDispatcher::instance().query(events::postprocess::GetPostProcessSettingsQuery{}).volumetricFog;
        }

        env::GISettings liveGi()
        {
            return events::EventDispatcher::instance().query(events::render::gi::GetGISettingsQuery{});
        }

        nlohmann::json giStatsJson()
        {
            const ::render::gi::GIDebugStats stats =
                events::EventDispatcher::instance().query(events::render::gi::GetGIDebugStatsQuery{});
            return {
                {"totalProbes", stats.totalProbes},
                {"activeCascades", stats.activeCascades},
                {"probesUpdatedThisFrame", stats.probesUpdatedThisFrame}
            };
        }

        // RenderPassHandler: the sky capture owns the ambient only while the atmosphere is enabled.
        bool dynamicSkyOwnsAmbient(const env::AtmosphereSettings& atmosphere)
        {
            return atmosphere.enabled && atmosphere.dynamicAmbient;
        }

        bool hasHdr(const std::optional<services::IBLData>& ibl)
        {
            return ibl.has_value() && ibl->hdrRef.isValid();
        }

        std::string skyMode(const env::AtmosphereSettings& atmosphere, const std::optional<services::IBLData>& ibl)
        {
            if (atmosphere.enabled)
            {
                return "atmosphere";
            }
            return hasHdr(ibl) ? "hdr" : "none";
        }

        std::string ambientMode(const env::AtmosphereSettings& atmosphere, const std::optional<services::IBLData>& ibl)
        {
            if (dynamicSkyOwnsAmbient(atmosphere))
            {
                return "dynamicSky";
            }
            return hasHdr(ibl) ? "hdr" : "default";
        }

        nlohmann::json hdrPathJson(const asset::AssetRef& ref)
        {
            if (!ref.isValid())
            {
                return nullptr;
            }
            const std::string& path = ref.resolve();
            if (path.empty())
            {
                return "guid:" + ref.toHexString();
            }
            // Engine paths are narrow strings.
            return projectRelativeUtf8(tryTerrainProjectRoot(), std::filesystem::path(path));
        }

        nlohmann::json iblJson(const std::optional<services::IBLData>& ibl)
        {
            if (!ibl.has_value())
            {
                return nullptr;
            }
            return {
                {"path", hdrPathJson(ibl->hdrRef)},
                {"intensity", env::floatJson(ibl->intensity)},
                {"rotationDeg", env::floatJson(ibl->rotationDeg)},
                {"tint", env::vec3Json(ibl->tint)}
            };
        }

        // The angle in degrees between the sun directions of two (azimuth, elevation) pairs.
        double sunSeparationDeg(double azimuthA, double elevationA, double azimuthB, double elevationB)
        {
            const auto direction = [](double azimuth, double elevation)
            {
                const double az = azimuth * std::numbers::pi / 180.0;
                const double el = elevation * std::numbers::pi / 180.0;
                return glm::dvec3(std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az));
            };
            const double cosine =
                std::clamp(glm::dot(direction(azimuthA, elevationA), direction(azimuthB, elevationB)), -1.0, 1.0);
            return std::acos(cosine) * 180.0 / std::numbers::pi;
        }

        // ------------------------------------------------------------------
        // The sun light
        // ------------------------------------------------------------------

        // What an edit asks of the scene's sun light.
        struct SunRequest
        {
            float azimuth = 0.0f;   // the sky angles the light should point away from
            float elevation = 0.0f;
            bool rotate = false;    // rotate an existing active light to those angles
            std::optional<glm::vec3> color;
            std::optional<float> intensity;
            bool createIfMissing = false; // create "Sun" when the scene has no directional light at all
        };

        // The reads behind a SunRequest, made before anything is written.
        struct SunPlan
        {
            SunRequest request;
            bool create = false;
            std::optional<services::EntityHandle> active;
            std::string activeName;
            bool rotate = false;
            services::TransformData transformBefore;
            services::TransformData transformAfter;
            bool patchLight = false;
            nlohmann::json lightBefore;
            nlohmann::json lightPatch;
            std::vector<std::string> warnings;

            bool changes() const { return create || rotate || patchLight; }
        };

        struct SunOutcome
        {
            std::optional<services::EntityHandle> entity;
            bool created = false;
            bool rotated = false;
            bool lightChanged = false;
        };

        nlohmann::json lightPatchJson(const SunRequest& request)
        {
            nlohmann::json patch = nlohmann::json::object();
            if (request.color.has_value())
            {
                patch["color"] = nlohmann::json::array({static_cast<double>(request.color->x),
                                                        static_cast<double>(request.color->y),
                                                        static_cast<double>(request.color->z)});
            }
            if (request.intensity.has_value())
            {
                patch["intensity"] = static_cast<double>(*request.intensity);
            }
            return patch;
        }

        // `current` is the light's component_get JSON (ComponentTools' DirectionalLight binding).
        bool lightDiffers(const nlohmann::json& current, const SunRequest& request)
        {
            if (request.color.has_value() && ArgReader::toVec3(current.at("color"), "color") != *request.color)
            {
                return true;
            }
            return request.intensity.has_value() && current.at("intensity").get<float>() != *request.intensity;
        }

        SunPlan planSun(const SunRequest& request)
        {
            SunPlan plan;
            plan.request = request;
            const bool lightRequested = request.color.has_value() || request.intensity.has_value();

            plan.active = env::sunEntity(true);
            if (!plan.active.has_value())
            {
                const std::optional<services::EntityHandle> any = env::sunEntity(false);
                if (!any.has_value() && request.createIfMissing)
                {
                    plan.create = true;
                }
                else if (any.has_value() && (request.rotate || lightRequested))
                {
                    const std::optional<services::EntityData> data = findEntity(*any);
                    plan.warnings.push_back("The scene's directional light " +
                                            entityText(*any, data.has_value() ? data->name : std::string("?")) +
                                            " is inactive, so it was not changed; activate it with entity_set_active");
                }
                else if (!any.has_value() && lightRequested)
                {
                    plan.warnings.push_back("sun.color / sun.intensity were not applied: the scene has no directional "
                                            "light (one is created when the atmosphere ends up enabled and "
                                            "createSunIfMissing is true)");
                }
                return plan;
            }

            const std::optional<services::EntityData> data = findEntity(*plan.active);
            if (!data.has_value())
            {
                throw std::runtime_error("The sun light entity " + std::to_string(toId(*plan.active)) + " does not exist");
            }
            plan.activeName = data->name;

            auto& dispatcher = events::EventDispatcher::instance();
            if (request.rotate)
            {
                events::scene::GetTransformQuery query;
                query.entity = *plan.active;
                const std::optional<services::TransformData> transform = dispatcher.query(query);
                if (!transform.has_value())
                {
                    throw std::runtime_error("The sun light " + entityText(*plan.active, plan.activeName) + " has no transform");
                }
                plan.transformBefore = *transform;
                plan.transformAfter = *transform;
                // Only the rotation: position and scale stay as they are.
                plan.transformAfter.rotation =
                    ::render::atmosphere::directionalLightEulerForSun(request.azimuth, request.elevation);
                plan.rotate = !(plan.transformAfter == plan.transformBefore);
            }

            if (lightRequested)
            {
                std::optional<nlohmann::json> current = getBuiltinComponent("DirectionalLight", *plan.active);
                if (!current.has_value())
                {
                    throw std::runtime_error("The sun light " + entityText(*plan.active, plan.activeName) +
                                             " has no DirectionalLight component");
                }
                plan.lightBefore = std::move(*current);
                plan.lightPatch = lightPatchJson(request);
                plan.patchLight = lightDiffers(plan.lightBefore, request);
            }
            return plan;
        }

        // entity_create + component_add DirectionalLight in one step, recorded like entity_create (one
        // EntityLifetimeUndo snapshot taken once the light is complete). A failure removes the half-built
        // entity again before it rethrows.
        services::EntityHandle createSun(undo::UndoRecorder& recorder, const SunRequest& request)
        {
            auto& dispatcher = events::EventDispatcher::instance();

            events::scene::CreateEntityCommand create;
            create.name = "Sun";
            const services::EntityHandle sun = dispatcher.execute(create);
            if (!sun.isValid())
            {
                throw std::runtime_error("Creating the 'Sun' entity failed");
            }

            try
            {
                if (!addBuiltinComponent("DirectionalLight", sun))
                {
                    throw std::runtime_error("Adding a DirectionalLight to the new 'Sun' entity failed");
                }

                events::scene::GetTransformQuery current;
                current.entity = sun;
                events::scene::SetTransformCommand place;
                place.entity = sun;
                place.transform = dispatcher.query(current).value_or(services::TransformData{});
                place.transform.rotation =
                    ::render::atmosphere::directionalLightEulerForSun(request.azimuth, request.elevation);
                dispatcher.execute(place);

                if (request.color.has_value() || request.intensity.has_value())
                {
                    if (!patchBuiltinComponent("DirectionalLight", sun, lightPatchJson(request)).has_value())
                    {
                        throw std::runtime_error("Setting the new sun light's color / intensity failed");
                    }
                }

                recorder.add(undo::EntityLifetimeUndo::capture(undo::EntityLifetimeUndo::Kind::Created, sun));
            }
            catch (...)
            {
                events::scene::DeleteEntityCommand remove;
                remove.entity = sun;
                try
                {
                    dispatcher.execute(remove);
                }
                catch (const std::exception&)
                {
                    // The original failure is the one to report.
                }
                throw;
            }
            return sun;
        }

        SunOutcome applySun(const SunPlan& plan, undo::UndoRecorder& recorder)
        {
            SunOutcome outcome;
            if (plan.create)
            {
                outcome.entity = createSun(recorder, plan.request);
                outcome.created = true;
                return outcome;
            }
            outcome.entity = plan.active;
            if (plan.rotate)
            {
                recorder.add(std::make_unique<undo::TransformUndo>(toId(*plan.active), plan.activeName,
                                                                   plan.transformBefore, plan.transformAfter));
                events::scene::SetTransformCommand command;
                command.entity = *plan.active;
                command.transform = plan.transformAfter;
                events::EventDispatcher::instance().execute(command);
                outcome.rotated = true;
            }
            if (plan.patchLight)
            {
                const std::optional<nlohmann::json> after = patchBuiltinComponent("DirectionalLight", *plan.active, plan.lightPatch);
                if (!after.has_value())
                {
                    throw std::runtime_error("The sun light " + entityText(*plan.active, plan.activeName) +
                                             " has no DirectionalLight component");
                }
                recorder.add(std::make_unique<undo::BuiltinComponentPatchUndo>(toId(*plan.active), "DirectionalLight",
                                                                               plan.lightBefore, *after));
                outcome.lightChanged = true;
            }
            return outcome;
        }

        nlohmann::json sunOutcomeJson(const env::AtmosphereSettings& atmosphere, const SunOutcome& outcome)
        {
            return {
                {"azimuth", env::floatJson(atmosphere.sunAzimuth)},
                {"elevation", env::floatJson(atmosphere.sunElevation)},
                {"entity", outcome.entity.has_value() ? entityId(*outcome.entity) : nlohmann::json(nullptr)},
                {"created", outcome.created},
                {"rotated", outcome.rotated},
                {"lightChanged", outcome.lightChanged}
            };
        }

        // ------------------------------------------------------------------
        // Weather
        // ------------------------------------------------------------------

        const char* precipitationName(::weather::PrecipitationType type)
        {
            switch (type)
            {
            case ::weather::PrecipitationType::None: return "None";
            case ::weather::PrecipitationType::Rain: return "Rain";
            case ::weather::PrecipitationType::Snow: return "Snow";
            }
            return "None";
        }

        nlohmann::json weatherStateJson(const ::weather::WeatherState& state)
        {
            return {
                {"cloudCoverage", env::floatJson(state.cloudCoverage)},
                {"cloudDensity", env::floatJson(state.cloudDensity)},
                {"cloudType", env::floatJson(state.cloudType)},
                {"precipType", precipitationName(state.precipType)},
                {"precipIntensity", env::floatJson(state.precipIntensity)},
                {"windSpeed", env::floatJson(state.windSpeed)},
                {"windDirectionDeg", env::floatJson(state.windDirectionDeg)},
                {"gustStrength", env::floatJson(state.gustStrength)},
                {"gustFrequency", env::floatJson(state.gustFrequency)},
                {"fogDensity", env::floatJson(state.fogDensity)},
                {"heightFogDensity", env::floatJson(state.heightFogDensity)},
                {"atmosphereTint", env::vec3Json(state.atmosphereTint)},
                {"ambientLightMult", env::floatJson(state.ambientLightMult)}
            };
        }

        // The preset every field of `state` matches (within presetTolerance), or null.
        nlohmann::json matchingPreset(const ::weather::WeatherState& state)
        {
            for (::weather::WeatherPresetId id : weatherPresets)
            {
                if (env::sameWeatherState(state, ::weather::getPreset(id), presetTolerance))
                {
                    return ::weather::presetIdToString(id);
                }
            }
            return nullptr;
        }

        // The live fog values weather drives.
        nlohmann::json effectiveFogJson(const env::FogSettings& fog)
        {
            return {
                {"enabled", fog.enabled},
                {"uniformDensity", env::floatJson(fog.uniformDensity)},
                {"heightFogDensity", env::floatJson(fog.heightFogDensity)},
                {"fogColor", nlohmann::json::array({env::floatJson(fog.fogColor[0]), env::floatJson(fog.fogColor[1]),
                                                    env::floatJson(fog.fogColor[2])})}
            };
        }

        float transitionProgress()
        {
            return events::EventDispatcher::instance().query(events::weather::GetWeatherTransitionProgressQuery{});
        }

        // "<section>.<key>" for every key of `patch` that weather drives (just "<key>" without a section).
        std::vector<std::string> weatherDrivenIn(const nlohmann::json& patch, const std::vector<std::string>& driven,
                                                 std::string_view section)
        {
            std::vector<std::string> keys;
            if (!patch.is_object())
            {
                return keys;
            }
            for (const std::string& key : driven)
            {
                if (patch.contains(key))
                {
                    keys.push_back(section.empty() ? key : std::string(section) + "." + key);
                }
            }
            return keys;
        }

        std::string weatherOverrideWarning(const std::vector<std::string>& keys)
        {
            std::string text = "Weather is enabled and overrides " + joinedText(keys) +
                               " live; the new values are saved with the scene and show when weather is off";
            const bool capturedBase = std::any_of(keys.begin(), keys.end(), [](const std::string& key)
            {
                return key == "atmosphere.sunIrradiance" || key == "atmosphere.aerialIntensity";
            });
            if (capturedBase)
            {
                text += " (atmosphere.sunIrradiance and aerialIntensity: after the next scene load, because weather "
                        "restores the values it captured when it started)";
            }
            return text;
        }

        // Every atmosphere / clouds key weather drives, as "<section>.<key>".
        nlohmann::json drivenFieldsJson()
        {
            nlohmann::json fields = nlohmann::json::array();
            for (const std::string& key : env::weatherDrivenKeys(env::atmosphereTable()))
            {
                fields.push_back("atmosphere." + key);
            }
            for (const std::string& key : env::weatherDrivenKeys(env::cloudTable()))
            {
                fields.push_back("clouds." + key);
            }
            return fields;
        }

        // ------------------------------------------------------------------
        // Fog
        // ------------------------------------------------------------------

        // volumetric_inject.glsl: density = uniformDensity + heightFogDensity at and below heightFogOffset
        // (noise ignored), extinction = (scatteringCoefficient + absorptionCoefficient) * density, and nothing is
        // fogged beyond maxDistance (the inject pass stores zero there). volumetric_raymarch.glsl treats an
        // extinction below raymarchExtinctionCutoff as clear air.
        nlohmann::json derivedFogJson(const env::FogSettings& fog)
        {
            const double extinction =
                (static_cast<double>(fog.scatteringCoefficient) + static_cast<double>(fog.absorptionCoefficient)) *
                (static_cast<double>(fog.uniformDensity) + static_cast<double>(fog.heightFogDensity));
            nlohmann::json out{
                {"extinctionAtGround", env::floatJson(static_cast<float>(extinction))},
                {"visibilityMeters", nullptr}
            };
            if (!fog.enabled)
            {
                out["note"] = "volumetric fog is disabled";
                return out;
            }
            if (extinction < raymarchExtinctionCutoff)
            {
                out["note"] = "the extinction is below the raymarch's cutoff (0.0001 per metre), so no fog is visible";
                return out;
            }
            const double visibility = lnTwenty / extinction;
            out["visibilityMeters"] = env::floatJson(static_cast<float>(visibility));
            if (visibility > static_cast<double>(fog.maxDistance))
            {
                out["limitedByMaxDistance"] = true;
                out["note"] = "the fog stops at maxDistance (" + numberText(fog.maxDistance) + " m), before 95% of the "
                              "light is gone, so objects beyond it stay visible through the fog";
            }
            return out;
        }

        nlohmann::json fogNotesJson()
        {
            return nlohmann::json::array({
                "There is no separate distance fog: uniformDensity is homogeneous fog everywhere within maxDistance of "
                "the camera. Height fog adds heightFogDensity at and below heightFogOffset (world metres) and thins "
                "by exp(-heightFogFalloff * metres above it); heightFogFalloff 0 keeps it the same at every height.",
                "Fog renders only while enabled is true.",
                "Light shafts appear where geometry shadows the sun's light inside the fog; they need the fog enabled "
                "and a directional light that casts shadows. Raise scatteringCoefficient for stronger shafts; "
                "anisotropy 0.6-0.9 (forward scattering) makes them brightest when looking toward the sun. They use "
                "the sun's virtual shadow map (the default): with ray-traced sun shadows enabled the fog is "
                "unshadowed, and the shafts fade out at the edge of the sun's shadow range (about 256 m from the "
                "camera with the default shadow settings).",
                "extinctionAtGround = (scatteringCoefficient + absorptionCoefficient) * (uniformDensity + "
                "heightFogDensity) per metre; visibilityMeters = ln(20) / extinction, where 95% of the light is gone "
                "(noise ignored). visibilityMeters is null while the fog is disabled or the extinction is below the "
                "raymarch's 0.0001 per-metre cutoff; limitedByMaxDistance marks a visibility beyond maxDistance, where "
                "the fog ends."
            });
        }

        std::string visibilityText(const nlohmann::json& derived)
        {
            const nlohmann::json& visibility = derived.at("visibilityMeters");
            return visibility.is_null() ? std::string("unlimited") : "~" + numberText(visibility.get<double>()) + " m";
        }

        // ------------------------------------------------------------------
        // environment_get
        // ------------------------------------------------------------------

        void registerEnvironmentGet(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "environment_get";
            tool.title = "Get environment";
            tool.description =
                "Read the scene's sky and lighting environment. 'atmosphere' and 'clouds' are the scene's SAVED "
                "settings (scene_save writes them to its .vfSettings; environment_set changes them); 'liveOverrides' "
                "lists the values the renderer uses right now that differ from them (weather overrides some while it "
                "is enabled, see weather.drivenFields). Also: timeOfDay (hours), dayNightEnabled, sun = the active "
                "directional light, which lights the scene and (unless dayNightEnabled) places the sky's sun: {entity, "
                "name, azimuth, elevation (degrees; azimuth 0 = toward +Z, 90 = toward +X; elevation 90 = overhead), "
                "color, intensity, inSync (it points where the saved sun angles say)} or null; sky 'atmosphere' "
                "(procedural) | 'hdr' (skybox) | 'none'; ambient 'dynamicSky' (sky capture) | 'hdr' | 'default'; ibl "
                "{path, intensity, rotationDeg, tint} or null; weather {enabled, drivenFields}; ranges = every "
                "atmosphere / clouds key with its type and valid range (includeRanges:false omits them).";
            tool.inputSchema = closedObject({
                {"includeRanges", schema::boolean("Include 'ranges' (the valid values for environment_set). Default true.")}
            });
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"includeRanges"}, "environment_get");
                const ArgReader reader(args);
                const bool includeRanges = reader.optBool("includeRanges", true);

                const types::RenderSettings authored = authoredSettings();
                const env::AtmosphereSettings atmosphere = liveAtmosphere();
                const env::CloudSettings clouds = liveClouds();
                const std::optional<services::IBLData> ibl = env::rootIbl();
                const bool weatherOn = env::isWeatherEnabled();
                std::vector<std::string> warnings;

                nlohmann::json out;
                out["atmosphere"] = env::toJson(env::atmosphereTable(), authored.atmosphere);
                out["clouds"] = env::toJson(env::cloudTable(), authored.cloud);
                out["liveOverrides"] = {
                    {"atmosphere", env::diffJson(env::atmosphereTable(), authored.atmosphere, atmosphere)},
                    {"clouds", env::diffJson(env::cloudTable(), authored.cloud, clouds)}
                };
                out["timeOfDay"] = env::floatJson(authored.atmosphere.timeOfDay);
                out["dayNightEnabled"] = authored.atmosphere.dayNightEnabled;

                std::string sunText = "none";
                out["sun"] = nullptr;
                if (const std::optional<services::EntityHandle> sun = env::sunEntity(true))
                {
                    const std::optional<services::EntityData> data = findEntity(*sun);
                    events::scene::GetTransformQuery transformQuery;
                    transformQuery.entity = *sun;
                    const std::optional<services::TransformData> transform =
                        events::EventDispatcher::instance().query(transformQuery);
                    events::scene::GetDirectionalLightDataQuery lightQuery;
                    lightQuery.entity = *sun;
                    const std::optional<services::DirectionalLightData> light =
                        events::EventDispatcher::instance().query(lightQuery);

                    const std::string name = data.has_value() ? data->name : std::string();
                    const glm::vec2 angles = transform.has_value()
                        ? ::render::atmosphere::sunAnglesFromDirectionalLightEuler(transform->rotation)
                        : glm::vec2(0.0f);
                    const bool inSync = transform.has_value() &&
                        sunSeparationDeg(angles.x, angles.y, authored.atmosphere.sunAzimuth,
                                         authored.atmosphere.sunElevation) < sunSyncToleranceDeg;
                    out["sun"] = {
                        {"entity", entityId(*sun)},
                        {"name", name},
                        {"azimuth", env::floatJson(angles.x)},
                        {"elevation", env::floatJson(angles.y)},
                        {"color", light.has_value() ? env::vec3Json(light->color) : nlohmann::json(nullptr)},
                        {"intensity", light.has_value() ? env::floatJson(light->intensity) : nlohmann::json(nullptr)},
                        {"inSync", inSync}
                    };
                    sunText = entityText(*sun, name);
                    if (!inSync && authored.atmosphere.dayNightEnabled)
                    {
                        warnings.push_back("The sun light points elsewhere than the saved sun angles, and with "
                                           "dayNightEnabled the sky uses the saved angles: sky and lighting disagree. "
                                           "environment_set {sun:{syncEntity:true}} rotates the light to the saved "
                                           "angles");
                    }
                }
                else if (const std::optional<services::EntityHandle> inactive = env::sunEntity(false))
                {
                    const std::optional<services::EntityData> data = findEntity(*inactive);
                    warnings.push_back("The scene's directional light " +
                                       entityText(*inactive, data.has_value() ? data->name : std::string("?")) +
                                       " is inactive, so nothing casts sun light; activate it with entity_set_active");
                }
                else if (atmosphere.enabled)
                {
                    warnings.push_back("The scene has no directional light, so nothing casts sun light or shadows; "
                                       "environment_set creates a 'Sun' (createSunIfMissing)");
                }

                out["sky"] = skyMode(atmosphere, ibl);
                out["ambient"] = ambientMode(atmosphere, ibl);
                out["ibl"] = iblJson(ibl);
                out["weather"] = {
                    {"enabled", weatherOn},
                    {"drivenFields", weatherOn ? drivenFieldsJson() : nlohmann::json::array()}
                };
                if (includeRanges)
                {
                    out["ranges"] = {
                        {"atmosphere", env::rangesJson(env::atmosphereTable())},
                        {"clouds", env::rangesJson(env::cloudTable())}
                    };
                }
                out["warnings"] = warnings;

                const std::string summary = "Sky: " + out["sky"].get<std::string>() + ", ambient: " +
                                            out["ambient"].get<std::string>() + ", time of day " +
                                            numberText(authored.atmosphere.timeOfDay) + " h, sun light " + sunText +
                                            ", weather " + (weatherOn ? "on" : "off") + ".";
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // environment_set
        // ------------------------------------------------------------------

        struct SunArgs
        {
            bool given = false;
            std::optional<float> azimuth;
            std::optional<float> elevation;
            std::optional<glm::vec3> color;
            std::optional<float> intensity;
            std::optional<bool> syncEntity; // unset: rotate the light when sun angles are requested
        };

        SunArgs readSunArgs(const nlohmann::json& args)
        {
            SunArgs sun;
            const auto it = args.find("sun");
            if (it == args.end() || it->is_null())
            {
                return sun;
            }
            env::rejectUnknownKeys(*it, {"azimuth", "elevation", "color", "intensity", "syncEntity"}, "sun");
            const nlohmann::json& object = *it;
            sun.given = true;
            if (object.contains("azimuth"))
            {
                sun.azimuth = env::requireFloat(object.at("azimuth"), 0.0, 360.0, "sun.azimuth");
            }
            if (object.contains("elevation"))
            {
                sun.elevation = env::requireFloat(object.at("elevation"), -90.0, 90.0, "sun.elevation");
            }
            if (object.contains("color"))
            {
                sun.color = env::requireVec3(object.at("color"), 0.0, 1.0, "sun.color");
            }
            if (object.contains("intensity"))
            {
                sun.intensity = env::requireFloat(object.at("intensity"), 0.0, maxSunIntensity, "sun.intensity");
            }
            if (object.contains("syncEntity"))
            {
                sun.syncEntity = env::requireBool(object.at("syncEntity"), "sun.syncEntity");
            }
            return sun;
        }

        void registerEnvironmentSet(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "environment_set";
            tool.title = "Set environment";
            tool.description =
                "Change the sky (procedural atmosphere), clouds, time of day and sun. 'atmosphere' and 'clouds' are "
                "PARTIAL updates of the saved settings: only the keys you pass change; an unknown key, a wrong type or "
                "an out-of-range value fails the whole call and changes nothing (environment_get lists every key and "
                "range). timeOfDay (hours: 6 sunrise, 12 noon, 18 sunset, 0 midnight) places the sun and moon exactly "
                "as the day-night cycle would; sun.azimuth / sun.elevation (degrees; azimuth 0 = toward +Z, 90 = "
                "toward +X) place the sun directly (not together with timeOfDay). The scene's active directional light "
                "is the sun: it is rotated to match whenever timeOfDay or sun angles are passed (sun.syncEntity:false "
                "leaves it; sun.syncEntity:true alone re-aims it at the saved angles), sun.color / sun.intensity change "
                "its light, and when the atmosphere ends up enabled and the scene has no directional light at all a "
                "'Sun' entity is created (createSunIfMissing:false prevents it). The day-night cycle "
                "(atmosphere.dayNightEnabled) only advances in Play mode. While weather is enabled it overrides "
                "sunIrradiance, aerialIntensity, ambientIntensity and the clouds' coverage, density, type, wind, tint "
                "and ambient live; the saved values still change. Saved with the scene on scene_save; one undo step; "
                "refused in Play mode. Returns {changed, atmosphere, clouds (the keys that changed, new values), "
                "timeOfDay, sun {azimuth, elevation, entity, created, rotated, lightChanged}, warnings}.";
            nlohmann::json sunSchema = closedObject({
                {"azimuth", schema::numberRange("Sun azimuth in degrees: 0 = toward +Z, 90 = toward +X.", 0.0, 360.0)},
                {"elevation", schema::numberRange("Sun elevation in degrees above the horizon: 90 = overhead, "
                                                  "negative = night.", -90.0, 90.0)},
                {"color", rgbSchema("Sun light colour [r, g, b], 0-1.")},
                {"intensity", schema::numberRange("Sun light intensity.", 0.0, maxSunIntensity)},
                {"syncEntity", schema::boolean("Rotate the scene's sun light to the sun angles. Default: true when "
                                               "angles are passed; true on its own re-aims the light at the saved "
                                               "angles.")}
            });
            sunSchema["description"] = "The sun: its sky angles and the scene's directional light.";
            tool.inputSchema = closedObject({
                {"atmosphere", env::schemaJson(env::atmosphereTable(),
                                               "Partial update of the saved atmosphere (procedural sky) settings.")},
                {"clouds", env::schemaJson(env::cloudTable(), "Partial update of the saved volumetric cloud settings.")},
                {"timeOfDay", schema::numberRange("Hours 0-24 (6 sunrise, 12 noon, 18 sunset, 0 midnight): places the "
                                                  "sun and moon as the day-night cycle would.", 0.0, 24.0)},
                {"sun", std::move(sunSchema)},
                {"createSunIfMissing", schema::boolean("Create a 'Sun' directional light when the atmosphere ends up "
                                                       "enabled and the scene has none. Default true.")}
            });
            tool.timeout = environmentTimeout;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                if (auto blocked = env::editBlocker("change the environment"))
                {
                    return ToolResult::error(*blocked);
                }
                env::rejectUnknownArgs(args, {"atmosphere", "clouds", "timeOfDay", "sun", "createSunIfMissing"},
                                       "environment_set");
                const ArgReader reader(args);
                const bool hasAtmosphere = reader.has("atmosphere");
                const bool hasClouds = reader.has("clouds");
                std::optional<float> timeOfDay;
                if (reader.has("timeOfDay"))
                {
                    timeOfDay = env::requireFloat(reader.raw("timeOfDay"), 0.0, 24.0, "timeOfDay");
                }
                const SunArgs sunArgs = readSunArgs(args);
                const bool createSunIfMissing = reader.has("createSunIfMissing")
                    ? env::requireBool(reader.raw("createSunIfMissing"), "createSunIfMissing")
                    : true;
                if (!hasAtmosphere && !hasClouds && !timeOfDay.has_value() && !sunArgs.given)
                {
                    throw ArgError("nothing to change: pass at least one of atmosphere, clouds, timeOfDay, sun");
                }
                if (timeOfDay.has_value() && (sunArgs.azimuth.has_value() || sunArgs.elevation.has_value()))
                {
                    throw ArgError("pass either timeOfDay or sun.azimuth / sun.elevation, not both: timeOfDay places "
                                   "the sun itself");
                }

                // Stage everything before anything is written.
                const types::RenderSettings before = authoredSettings();
                env::AtmosphereSettings atmosphere = hasAtmosphere
                    ? env::strictPatch(env::atmosphereTable(), before.atmosphere, reader.raw("atmosphere"), "atmosphere")
                    : before.atmosphere;
                if (timeOfDay.has_value())
                {
                    // Echoing the current moon angles (e.g. a whole environment_get object) is fine; a new value
                    // would be overwritten by timeOfDay, so it is refused instead of silently dropped.
                    if (atmosphere.moonAzimuth != before.atmosphere.moonAzimuth ||
                        atmosphere.moonElevation != before.atmosphere.moonElevation)
                    {
                        throw ArgError("pass either timeOfDay or a new atmosphere.moonAzimuth / moonElevation, not "
                                       "both: timeOfDay places the moon itself");
                    }
                    const ::render::atmosphere::DayNightAngles angles =
                        ::render::atmosphere::dayNightAngles(*timeOfDay, atmosphere.moonPhaseOffset);
                    atmosphere.timeOfDay = *timeOfDay;
                    atmosphere.sunAzimuth = angles.sunAzimuth;
                    atmosphere.sunElevation = angles.sunElevation;
                    atmosphere.moonAzimuth = angles.moonAzimuth;
                    atmosphere.moonElevation = angles.moonElevation;
                }
                if (sunArgs.azimuth.has_value())
                {
                    atmosphere.sunAzimuth = *sunArgs.azimuth;
                }
                if (sunArgs.elevation.has_value())
                {
                    atmosphere.sunElevation = *sunArgs.elevation;
                }
                const env::CloudSettings clouds = hasClouds
                    ? env::strictPatch(env::cloudTable(), before.cloud, reader.raw("clouds"), "clouds")
                    : before.cloud;
                const nlohmann::json atmosphereDiff = env::diffJson(env::atmosphereTable(), before.atmosphere, atmosphere);
                const nlohmann::json cloudDiff = env::diffJson(env::cloudTable(), before.cloud, clouds);

                // The light follows requested sun angles unless syncEntity is false; syncEntity:true on its own
                // re-aims it at the saved angles.
                const bool anglesGiven =
                    timeOfDay.has_value() || sunArgs.azimuth.has_value() || sunArgs.elevation.has_value();
                SunRequest request;
                request.azimuth = atmosphere.sunAzimuth;
                request.elevation = atmosphere.sunElevation;
                request.rotate = sunArgs.syncEntity.value_or(anglesGiven);
                request.color = sunArgs.color;
                request.intensity = sunArgs.intensity;
                request.createIfMissing = createSunIfMissing && atmosphere.enabled;
                const SunPlan plan = planSun(request);

                std::vector<std::string> warnings = plan.warnings;
                if (sunArgs.syncEntity == false && anglesGiven && plan.active.has_value() && !atmosphere.dayNightEnabled)
                {
                    warnings.push_back("sun.syncEntity is false: the sky's sun follows the directional light " +
                                       entityText(*plan.active, plan.activeName) + ", which keeps its rotation, so the "
                                       "new sun angles are saved but not visible");
                }
                if (env::isWeatherEnabled())
                {
                    // Only keys that actually changed: an echoed environment_get object must not warn about every key.
                    std::vector<std::string> driven =
                        weatherDrivenIn(atmosphereDiff, env::weatherDrivenKeys(env::atmosphereTable()), "atmosphere");
                    for (std::string& key : weatherDrivenIn(cloudDiff, env::weatherDrivenKeys(env::cloudTable()), "clouds"))
                    {
                        driven.push_back(std::move(key));
                    }
                    if (!driven.empty())
                    {
                        warnings.push_back(weatherOverrideWarning(driven));
                    }
                }

                const bool changed = !atmosphereDiff.empty() || !cloudDiff.empty() || plan.changes();
                SunOutcome outcome;
                outcome.entity = plan.active;
                if (changed)
                {
                    undo::UndoRecorder recorder("MCP: Set environment");
                    try
                    {
                        // Each undo entry is recorded before its write: a rollback then also restores a
                        // write that failed half-way.
                        if (!atmosphereDiff.empty())
                        {
                            writeRecorded(recorder, before.atmosphere, atmosphere);
                        }
                        if (!cloudDiff.empty())
                        {
                            writeRecorded(recorder, before.cloud, clouds);
                        }
                        outcome = applySun(plan, recorder);
                    }
                    catch (const std::exception& e)
                    {
                        recorder.rollback();
                        return ToolResult::error(std::string("The environment was not changed (rolled back): ") + e.what());
                    }
                    recorder.push();
                }

                nlohmann::json out{
                    {"changed", changed},
                    {"atmosphere", atmosphereDiff},
                    {"clouds", cloudDiff},
                    {"timeOfDay", env::floatJson(atmosphere.timeOfDay)},
                    {"sun", sunOutcomeJson(atmosphere, outcome)},
                    {"note", changed ? "Saved with the scene on scene_save; one undo step."
                                     : "Nothing changed, so no undo step was recorded."},
                    {"warnings", warnings}
                };

                std::string summary;
                if (!changed)
                {
                    summary = "Nothing changed.";
                }
                else
                {
                    std::vector<std::string> parts;
                    if (!atmosphereDiff.empty())
                    {
                        parts.push_back("atmosphere (" + std::to_string(atmosphereDiff.size()) + " keys)");
                    }
                    if (!cloudDiff.empty())
                    {
                        parts.push_back("clouds (" + std::to_string(cloudDiff.size()) + " keys)");
                    }
                    if (outcome.created)
                    {
                        parts.push_back("created sun light " + std::to_string(toId(*outcome.entity)));
                    }
                    if (outcome.rotated)
                    {
                        parts.push_back("rotated sun light " + std::to_string(toId(*outcome.entity)));
                    }
                    if (outcome.lightChanged)
                    {
                        parts.push_back("sun light colour / intensity");
                    }
                    summary = "Environment updated: " + joinedText(parts) + ".";
                }
                return reply(std::move(out), std::move(summary));
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // ibl_set
        // ------------------------------------------------------------------

        // A project .vfHdr as the asset reference the IBLComponent stores.
        asset::AssetRef resolveHdr(const std::string& path)
        {
            if (path.empty())
            {
                throw ArgError("argument 'path' must not be empty");
            }
            const std::filesystem::path root = terrainProjectRoot();
            std::string error;
            const std::optional<std::filesystem::path> resolved = resolveInside(root, path, error, ".vfHdr");
            if (!resolved.has_value())
            {
                throw ArgError("argument 'path': " + error);
            }
            std::error_code ec;
            if (!std::filesystem::is_regular_file(*resolved, ec))
            {
                throw ArgError("argument 'path': '" + path + "' does not exist (assets_list shows the project's .vfHdr "
                               "files; assets_import converts .hdr / .exr images)");
            }
            // Narrow, like every engine path: AssetRef::fromPath resolves it against the asset database.
            asset::AssetRef ref = asset::AssetRef::fromPath(resolved->string());
            if (!ref.isValid())
            {
                throw ArgError("argument 'path': '" + path + "' is not an asset the asset database knows (import the "
                               "HDR image with assets_import)");
            }
            return ref;
        }

        void rejectArgsFor(const ArgReader& reader, std::initializer_list<const char*> names, const std::string& source,
                           const char* hint)
        {
            for (const char* name : names)
            {
                if (reader.has(name))
                {
                    throw ArgError("argument '" + std::string(name) + "' does not apply to source '" + source + "' (" +
                                   hint + ")");
                }
            }
        }

        nlohmann::json iblStateJson(const std::string& source)
        {
            const env::AtmosphereSettings atmosphere = liveAtmosphere();
            const std::optional<services::IBLData> ibl = env::rootIbl();
            return {
                {"source", source},
                {"ibl", iblJson(ibl)},
                {"sky", skyMode(atmosphere, ibl)},
                {"ambient", ambientMode(atmosphere, ibl)}
            };
        }

        ToolResult iblSetHdr(const ArgReader& reader)
        {
            std::optional<float> intensity;
            if (reader.has("intensity"))
            {
                intensity = env::requireFloat(reader.raw("intensity"), 0.0, maxIblIntensity, "intensity");
            }
            std::optional<float> rotationDeg;
            if (reader.has("rotationDeg"))
            {
                rotationDeg = env::requireFloat(reader.raw("rotationDeg"), 0.0, 360.0, "rotationDeg");
            }
            std::optional<glm::vec3> tint;
            if (reader.has("tint"))
            {
                tint = env::requireVec3(reader.raw("tint"), 0.0, 1.0, "tint");
            }
            const bool showAsSky = reader.has("showAsSky") ? env::requireBool(reader.raw("showAsSky"), "showAsSky") : true;

            const std::optional<services::IBLData> current = env::rootIbl();
            asset::AssetRef hdr;
            if (reader.has("path"))
            {
                hdr = resolveHdr(reader.requireString("path"));
            }
            else if (hasHdr(current))
            {
                hdr = current->hdrRef; // knob-only change of the current HDR
            }
            else
            {
                throw ArgError("argument 'path' is required: the scene has no HDR environment yet (pass a "
                               "project-relative .vfHdr)");
            }

            const services::IBLData defaults{};
            services::IBLData data;
            data.hdrRef = hdr;
            data.intensity = intensity.value_or(current.has_value() ? current->intensity : defaults.intensity);
            data.rotationDeg = rotationDeg.value_or(current.has_value() ? current->rotationDeg : defaults.rotationDeg);
            data.tint = tint.value_or(current.has_value() ? current->tint : defaults.tint);
            const bool rebake = !hasHdr(current) || current->hdrRef != hdr;
            const std::optional<services::IBLData> after = data;
            const bool iblChanged = !env::sameIbl(current, after);

            // The sky flags: an HDR shown as the sky means no procedural sky and no sky-captured ambient.
            const types::RenderSettings settings = authoredSettings();
            env::AtmosphereSettings atmosphere = settings.atmosphere;
            if (showAsSky)
            {
                atmosphere.enabled = false;
                atmosphere.dynamicAmbient = false;
            }
            const nlohmann::json atmosphereDiff = env::diffJson(env::atmosphereTable(), settings.atmosphere, atmosphere);

            std::vector<std::string> warnings;
            if (!showAsSky && dynamicSkyOwnsAmbient(atmosphere))
            {
                warnings.push_back("The HDR is neither visible nor used for lighting while the dynamic sky owns the "
                                   "ambient light (atmosphere.enabled + dynamicAmbient); pass showAsSky:true, or turn "
                                   "atmosphere.dynamicAmbient off with environment_set");
            }

            const bool changed = iblChanged || !atmosphereDiff.empty();
            if (changed)
            {
                undo::UndoRecorder recorder("MCP: Set IBL (hdr)");
                try
                {
                    // The IBL first: while the dynamic sky owns the ambient, SetIBL only stages the HDR, and the
                    // atmosphere's dynamicAmbient ON -> OFF edge below bakes it once.
                    if (iblChanged)
                    {
                        recorder.add(std::make_unique<undo::IblEnvironmentUndo>(current, after));
                        env::applyIbl(after, rebake);
                    }
                    if (!atmosphereDiff.empty())
                    {
                        writeRecorded(recorder, settings.atmosphere, atmosphere);
                    }
                }
                catch (const std::exception& e)
                {
                    recorder.rollback();
                    return ToolResult::error(std::string("The IBL was not changed (rolled back): ") + e.what());
                }
                recorder.push();
            }

            // While the dynamic sky still owns the ambient, SetIBL only stages the HDR (RenderPassHandler::
            // applyHdrEnvironment); it is baked later, on the dynamicAmbient ON -> OFF edge.
            const bool rebaked = iblChanged && rebake && !dynamicSkyOwnsAmbient(atmosphere);
            nlohmann::json out = iblStateJson("hdr");
            out["changed"] = changed;
            out["rebaked"] = rebaked;
            out["atmosphere"] = atmosphereDiff;
            out["warnings"] = warnings;

            std::string summary = "Nothing changed.";
            if (changed)
            {
                const nlohmann::json& ibl = out.at("ibl");
                const std::string hdrName = ibl.is_object() && ibl.at("path").is_string()
                    ? ibl.at("path").get<std::string>()
                    : std::string("?");
                summary = "IBL: HDR " + hdrName + (rebaked ? " baked" : "") + "; sky: " +
                          out.at("sky").get<std::string>() + ", ambient: " + out.at("ambient").get<std::string>() + ".";
            }
            return reply(std::move(out), std::move(summary));
        }

        ToolResult iblSetDynamicSky(const ArgReader& reader)
        {
            std::optional<float> ambientIntensity;
            if (reader.has("ambientIntensity"))
            {
                ambientIntensity = env::requireFloat(reader.raw("ambientIntensity"), 0.0, maxAmbientIntensity,
                                                     "ambientIntensity");
            }
            const bool createSunIfMissing = reader.has("createSunIfMissing")
                ? env::requireBool(reader.raw("createSunIfMissing"), "createSunIfMissing")
                : true;

            const types::RenderSettings settings = authoredSettings();
            env::AtmosphereSettings atmosphere = settings.atmosphere;
            atmosphere.enabled = true;
            atmosphere.dynamicAmbient = true;
            if (ambientIntensity.has_value())
            {
                atmosphere.ambientIntensity = *ambientIntensity;
            }
            const nlohmann::json atmosphereDiff = env::diffJson(env::atmosphereTable(), settings.atmosphere, atmosphere);

            SunRequest request;
            request.azimuth = atmosphere.sunAzimuth;
            request.elevation = atmosphere.sunElevation;
            request.createIfMissing = createSunIfMissing;
            const SunPlan plan = planSun(request);

            std::vector<std::string> warnings = plan.warnings;
            if (ambientIntensity.has_value() && env::isWeatherEnabled())
            {
                warnings.push_back(weatherOverrideWarning({"atmosphere.ambientIntensity"}));
            }

            const bool changed = !atmosphereDiff.empty() || plan.changes();
            SunOutcome outcome;
            outcome.entity = plan.active;
            if (changed)
            {
                undo::UndoRecorder recorder("MCP: Set IBL (dynamic sky)");
                try
                {
                    if (!atmosphereDiff.empty())
                    {
                        writeRecorded(recorder, settings.atmosphere, atmosphere);
                    }
                    outcome = applySun(plan, recorder);
                }
                catch (const std::exception& e)
                {
                    recorder.rollback();
                    return ToolResult::error(std::string("The IBL was not changed (rolled back): ") + e.what());
                }
                recorder.push();
            }

            nlohmann::json out = iblStateJson("dynamicSky");
            out["changed"] = changed;
            out["atmosphere"] = atmosphereDiff;
            out["sun"] = sunOutcomeJson(atmosphere, outcome);
            out["note"] = "The sky capture refreshes itself, time-sliced over a few frames.";
            out["warnings"] = warnings;
            const std::string summary = changed
                ? "IBL: dynamic sky lights the scene" + std::string(outcome.created ? "; created a 'Sun' light" : "") + "."
                : std::string("Nothing changed.");
            return reply(std::move(out), summary);
        }

        ToolResult iblSetNone()
        {
            const std::optional<services::IBLData> current = env::rootIbl();
            std::vector<std::string> warnings;
            if (dynamicSkyOwnsAmbient(liveAtmosphere()))
            {
                warnings.push_back("Ambient light still comes from the dynamic sky (atmosphere.dynamicAmbient); "
                                   "environment_set {atmosphere:{dynamicAmbient:false}} turns it off");
            }

            const bool changed = current.has_value();
            if (changed)
            {
                undo::UndoRecorder recorder("MCP: Set IBL (none)");
                try
                {
                    recorder.add(std::make_unique<undo::IblEnvironmentUndo>(current, std::nullopt));
                    env::applyIbl(std::nullopt, false);
                }
                catch (const std::exception& e)
                {
                    recorder.rollback();
                    return ToolResult::error(std::string("The IBL was not removed (rolled back): ") + e.what());
                }
                recorder.push();
            }

            nlohmann::json out = iblStateJson("none");
            out["changed"] = changed;
            out["warnings"] = warnings;
            return reply(std::move(out), changed ? "Removed the HDR environment." : "The scene has no IBL; nothing changed.");
        }

        void registerIblSet(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "ibl_set";
            tool.title = "Set image-based lighting";
            tool.description =
                "Choose the image-based environment: what the visible sky is and where ambient light and reflections "
                "come from. source 'hdr': the scene's IBL becomes a project .vfHdr ('path', project-relative; "
                "assets_import converts .hdr / .exr images); omit 'path' to only change intensity (0-5), rotationDeg "
                "or tint of the current HDR. With showAsSky (default true) the procedural atmosphere and its dynamic "
                "ambient are turned off, so the HDR is both the visible skybox and the source of ambient light and "
                "reflections; showAsSky:false leaves the atmosphere flags unchanged (the HDR then lights the scene only "
                "while the dynamic sky does not own the ambient). A new HDR is baked right away (blocking, can take "
                "seconds) unless the dynamic sky still owns the ambient ('rebaked'). source 'dynamicSky': the "
                "procedural atmosphere is the sky AND lights the scene (atmosphere.enabled + dynamicAmbient; "
                "ambientIntensity 0-10 scales it); a 'Sun' directional light is created when the scene has no "
                "directional light (createSunIfMissing:false prevents it). "
                "source 'none': remove the HDR environment (ambient then comes from the dynamic sky if it is on, "
                "else a neutral default). Saved with the scene (the IBL on the scene root, the sky flags in the "
                "atmosphere settings); one undo step; refused in Play mode. Returns {changed, source, ibl, sky, "
                "ambient, atmosphere (changed keys), rebaked, sun, warnings}.";
            tool.inputSchema = closedObject({
                {"source", schema::enumString("hdr = an HDR image; dynamicSky = the procedural sky; none = remove "
                                              "the HDR.", iblSources)},
                {"path", schema::string("hdr: project-relative .vfHdr. Omit to keep the current HDR.")},
                {"intensity", schema::numberRange("hdr: ambient / reflection intensity. Default: unchanged (1 for a "
                                                  "new IBL).", 0.0, maxIblIntensity)},
                {"rotationDeg", schema::numberRange("hdr: rotation of the environment around +Y, degrees. Default: "
                                                    "unchanged (0).", 0.0, 360.0)},
                {"tint", rgbSchema("hdr: colour multiplier [r, g, b], 0-1. Default: unchanged (white).")},
                {"showAsSky", schema::boolean("hdr: true (default) turns the procedural sky and the dynamic ambient "
                                              "off, so the HDR is both the visible sky and the ambient source; false "
                                              "leaves the atmosphere flags unchanged.")},
                {"ambientIntensity", schema::numberRange("dynamicSky: multiplier on the captured sky ambient.", 0.0,
                                                         maxAmbientIntensity)},
                {"createSunIfMissing", schema::boolean("dynamicSky: create a 'Sun' directional light when the scene "
                                                       "has none. Default true.")}
            }, {"source"});
            tool.timeout = iblTimeout;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                if (auto blocked = env::editBlocker("change the image-based lighting"))
                {
                    return ToolResult::error(*blocked);
                }
                env::rejectUnknownArgs(args, {"source", "path", "intensity", "rotationDeg", "tint", "showAsSky",
                                              "ambientIntensity", "createSunIfMissing"}, "ibl_set");
                const ArgReader reader(args);
                const std::string source = reader.requireEnum("source", iblSources);
                if (source == "hdr")
                {
                    rejectArgsFor(reader, {"ambientIntensity", "createSunIfMissing"}, source,
                                  "it applies to source dynamicSky");
                    return iblSetHdr(reader);
                }
                if (source == "dynamicSky")
                {
                    rejectArgsFor(reader, {"path", "intensity", "rotationDeg", "tint", "showAsSky"}, source,
                                  "it applies to source hdr");
                    return iblSetDynamicSky(reader);
                }
                rejectArgsFor(reader, {"path", "intensity", "rotationDeg", "tint", "showAsSky", "ambientIntensity",
                                       "createSunIfMissing"}, source, "source none takes no other arguments");
                return iblSetNone();
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // ibl_bake
        // ------------------------------------------------------------------

        void registerIblBake(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "ibl_bake";
            tool.title = "Bake lighting";
            tool.description =
                "Re-capture lighting. target 'environment': re-bakes the scene's HDR environment now (blocking); a "
                "dynamic sky needs no bake (it re-captures itself, time-sliced over a few frames). target 'probes': "
                "queues the reflection probes (all, or the one 'entity') to re-capture over the next frames "
                "(asynchronous, about 36 work items per probe) - do it after moving geometry or changing the sky, "
                "then give it a moment before viewport_screenshot. 'all' (default) does both. Changes no scene data: "
                "not undoable; allowed in Play mode. Returns {environment {mode, baked}, probes {queued, async}, "
                "warnings}.";
            tool.inputSchema = closedObject({
                {"target", schema::enumString("What to bake. Default all.", bakeTargets)},
                {"entity", schema::entity("probes / all: only this reflection probe entity. Default: every probe.")}
            });
            tool.timeout = iblTimeout;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"target", "entity"}, "ibl_bake");
                const ArgReader reader(args);
                const std::string target = reader.optEnum("target", bakeTargets, "all");
                const std::optional<uint32_t> probe = reader.optEntity("entity");
                if (probe.has_value() && target == "environment")
                {
                    throw ArgError("argument 'entity' names a reflection probe: it needs target 'probes' or 'all'");
                }

                auto& dispatcher = events::EventDispatcher::instance();
                nlohmann::json out = nlohmann::json::object();
                std::vector<std::string> parts;

                // Probes first: a bad 'entity' fails before the blocking environment bake.
                if (target != "environment")
                {
                    events::scene::BakeReflectionProbesCommand bake;
                    if (probe.has_value())
                    {
                        bake.entity = toHandle(*probe);
                    }
                    const bool queued = dispatcher.execute(bake);
                    if (!queued && probe.has_value())
                    {
                        return ToolResult::error("Entity " + std::to_string(*probe) + " is not a reflection probe (or does "
                                                 "not exist); omit 'entity' to bake every probe");
                    }
                    out["probes"] = queued
                        ? nlohmann::json{
                              {"queued", true},
                              {"async", true},
                              {"entity", probe.has_value() ? nlohmann::json(*probe) : nlohmann::json(nullptr)},
                              {"note", "bakes over the next frames (about 36 work items per probe)"}
                          }
                        : nlohmann::json{{"queued", false}, {"async", false}, {"note", "no reflection probes in the scene"}};
                    parts.push_back(queued ? "probes queued" : "no reflection probes");
                }

                if (target != "probes")
                {
                    const env::AtmosphereSettings atmosphere = liveAtmosphere();
                    const std::optional<services::IBLData> ibl = env::rootIbl();
                    if (dynamicSkyOwnsAmbient(atmosphere))
                    {
                        out["environment"] = {
                            {"mode", "dynamicSky"},
                            {"baked", false},
                            {"note", "the dynamic sky is captured automatically, time-sliced over a few frames"}
                        };
                        parts.push_back("dynamic sky (captured automatically)");
                    }
                    else if (hasHdr(ibl))
                    {
                        const std::string path = ibl->hdrRef.resolve();
                        if (path.empty())
                        {
                            return ToolResult::error("The scene's HDR asset guid:" + ibl->hdrRef.toHexString() +
                                                     " no longer resolves to a file; choose one with ibl_set");
                        }
                        events::render::SetIBLCommand bake;
                        bake.hdrPath = path;
                        if (!dispatcher.execute(bake))
                        {
                            return ToolResult::error("The renderer could not load the HDR environment '" +
                                                     pathToUtf8(std::filesystem::path(path)) + "' (see logs_read)");
                        }
                        out["environment"] = {{"mode", "hdr"}, {"baked", true}, {"path", hdrPathJson(ibl->hdrRef)}};
                        parts.push_back("HDR environment baked");
                    }
                    else
                    {
                        out["environment"] = {
                            {"mode", "none"},
                            {"baked", false},
                            {"note", "no HDR environment and no dynamic sky: nothing to bake (see ibl_set)"}
                        };
                        parts.push_back("no environment to bake");
                    }
                }

                out["warnings"] = nlohmann::json::array();
                return reply(std::move(out), joinedText(parts) + ".");
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // gi_get / gi_set / gi_debug
        // ------------------------------------------------------------------

        nlohmann::json giDebugJson(const env::GISettings& live)
        {
            return {
                {"probes", live.showProbes},
                {"cascadeBounds", live.showCascadeBounds},
                {"probeValidity", live.showProbeValidity}
            };
        }

        bool giActive(const env::GISettings& live)
        {
            return events::EventDispatcher::instance().query(events::render::gi::IsGIEnabledQuery{}) &&
                   live.quality != ::render::gi::GIQuality::Off;
        }

        std::string qualityName(::render::gi::GIQuality quality)
        {
            const auto index = static_cast<std::size_t>(quality);
            return index < env::giQualityNames.size() ? std::string(env::giQualityNames[index]) : std::to_string(index);
        }

        void registerGiGet(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "gi_get";
            tool.title = "Get global illumination";
            tool.description =
                "Read global illumination: radiance-cascade probes (multi-bounce indirect light, traced with GPU ray "
                "queries) plus optional screen-space GI. 'authored' = the scene's saved GI settings (scene_save), "
                "'live' = what the renderer runs, active = enabled with quality above Off, stats {totalProbes, "
                "activeCascades, probesUpdatedThisFrame} (about 256 / 800 / 1088 probes at Medium / High / Ultra), "
                "debug = the live debug views (gi_debug), presets = the gi_set quality presets, ranges = every gi_set "
                "'settings' key with its range (includeRanges:false omits them). To see the bounce light, compare "
                "viewport_screenshot with GI off and on.";
            tool.inputSchema = closedObject({
                {"includeRanges", schema::boolean("Include 'ranges' (the valid gi_set settings). Default true.")}
            });
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"includeRanges"}, "gi_get");
                const ArgReader reader(args);
                const bool includeRanges = reader.optBool("includeRanges", true);

                const env::GISettings authored = authoredSettings().gi;
                const env::GISettings live = liveGi();
                const bool active = giActive(live);
                std::vector<std::string> warnings;
                const nlohmann::json liveDiff = env::diffJson(env::giTable(), authored, live);
                if (!liveDiff.empty())
                {
                    std::vector<std::string> keys;
                    for (auto it = liveDiff.begin(); it != liveDiff.end(); ++it)
                    {
                        keys.push_back(it.key());
                    }
                    warnings.push_back("The live GI differs from the saved settings in " + joinedText(keys) +
                                       " (the saved values apply when the scene loads; gi_set writes both)");
                }

                nlohmann::json out{
                    {"authored", env::toJson(env::giTable(), authored)},
                    {"live", env::toJson(env::giTable(), live)},
                    {"active", active},
                    {"stats", giStatsJson()},
                    {"debug", giDebugJson(live)},
                    {"presets", giPresetNames()},
                    {"warnings", warnings}
                };
                if (includeRanges)
                {
                    out["ranges"] = env::rangesJson(env::giTable());
                }
                const std::string summary = std::string("GI ") + (active ? "active" : "inactive") + " (quality " +
                                            qualityName(live.quality) + ", " +
                                            std::to_string(out["stats"]["totalProbes"].get<uint32_t>()) + " probes).";
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }

        void registerGiSet(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "gi_set";
            tool.title = "Set global illumination";
            tool.description =
                "Change global illumination. preset Off | Medium | High | Ultra resets EVERY GI setting to that "
                "quality preset (High adds far-field cascades and screen-space GI); 'settings' is a partial update "
                "applied on top of it, or on the saved settings when no preset is given - strict like "
                "environment_set (gi_get lists every key and range). Structural changes (quality, enabled, probe "
                "spacing, cascade multiplier, far-field grid) rebuild the probes ('rebuilt'). Saved with the scene on "
                "scene_save and applied again when the scene loads; one undo step; refused in Play mode. Debug views: "
                "gi_debug. Returns {changed, preset, diff, settings, rebuilt, stats, warnings}.";
            tool.inputSchema = closedObject({
                {"preset", schema::enumString("Quality preset; resets every GI setting to it first.", giPresetNames())},
                {"settings", env::schemaJson(env::giTable(), "Partial update of the GI settings.")}
            });
            tool.timeout = giTimeout;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                if (auto blocked = env::editBlocker("change global illumination"))
                {
                    return ToolResult::error(*blocked);
                }
                env::rejectUnknownArgs(args, {"preset", "settings"}, "gi_set");
                const ArgReader reader(args);
                if (!reader.has("preset") && !reader.has("settings"))
                {
                    throw ArgError("nothing to change: pass 'preset' and / or 'settings'");
                }
                std::optional<::render::gi::GIQuality> preset;
                if (reader.has("preset"))
                {
                    const std::string name = reader.requireEnum("preset", giPresetNames());
                    const auto& names = giPresetNames();
                    preset = static_cast<::render::gi::GIQuality>(std::find(names.begin(), names.end(), name) - names.begin());
                }

                const env::GISettings before = authoredSettings().gi;
                env::GISettings staged = preset.has_value() ? env::GISettings::fromQuality(*preset) : before;
                if (reader.has("settings"))
                {
                    staged = env::strictPatch(env::giTable(), staged, reader.raw("settings"), "settings");
                }
                const nlohmann::json diff = env::diffJson(env::giTable(), before, staged);
                const env::GISettings liveBefore = liveGi();

                bool rebuilt = false;
                if (!diff.empty())
                {
                    undo::UndoRecorder recorder("MCP: Set GI");
                    try
                    {
                        writeRecorded(recorder, before, staged);
                    }
                    catch (const std::exception& e)
                    {
                        recorder.rollback();
                        return ToolResult::error(std::string("GI was not changed (rolled back): ") + e.what());
                    }
                    recorder.push();
                    rebuilt = ::render::gi::giSettingsRequireRebuild(liveBefore, staged);
                }

                std::vector<std::string> warnings;
                if (staged.quality != ::render::gi::GIQuality::Off && !staged.enabled)
                {
                    warnings.push_back("GI is configured (quality " + qualityName(staged.quality) +
                                       ") but disabled; pass settings {enabled:true} to turn it on");
                }
                if (staged.enabled && staged.quality == ::render::gi::GIQuality::Off && !staged.ssgiEnabled)
                {
                    warnings.push_back("GI is enabled with quality Off and screen-space GI off, which does nothing; "
                                       "choose a preset (Medium or higher) or enable ssgiEnabled");
                }

                nlohmann::json out{
                    {"changed", !diff.empty()},
                    {"preset", preset.has_value() ? nlohmann::json(qualityName(*preset)) : nlohmann::json(nullptr)},
                    {"diff", diff},
                    {"settings", env::toJson(env::giTable(), staged)},
                    {"rebuilt", rebuilt},
                    {"stats", giStatsJson()},
                    {"warnings", warnings}
                };
                const std::string summary = diff.empty()
                    ? std::string("Nothing changed.")
                    : "GI updated (quality " + qualityName(staged.quality) + (staged.enabled ? ", enabled" : ", disabled") +
                          (rebuilt ? ", probes rebuilt" : "") + ").";
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }

        void registerGiDebug(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "gi_debug";
            tool.title = "GI debug views";
            tool.description =
                "Toggle the GI debug views in the viewport: probes, cascadeBounds and probeValidity. Session-only: "
                "not saved and not undoable; allowed in Play mode. The views draw only while GI is active (gi_set "
                "quality Medium or higher). Returns {debug, active, warnings}.";
            tool.inputSchema = closedObject({
                {"probes", schema::boolean("Show the probes.")},
                {"cascadeBounds", schema::boolean("Show the cascade bounds.")},
                {"probeValidity", schema::boolean("Show the probe validity.")}
            });
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"probes", "cascadeBounds", "probeValidity"}, "gi_debug");
                const ArgReader reader(args);
                std::optional<bool> probes;
                std::optional<bool> cascadeBounds;
                std::optional<bool> probeValidity;
                if (reader.has("probes"))
                {
                    probes = env::requireBool(reader.raw("probes"), "probes");
                }
                if (reader.has("cascadeBounds"))
                {
                    cascadeBounds = env::requireBool(reader.raw("cascadeBounds"), "cascadeBounds");
                }
                if (reader.has("probeValidity"))
                {
                    probeValidity = env::requireBool(reader.raw("probeValidity"), "probeValidity");
                }
                if (!probes.has_value() && !cascadeBounds.has_value() && !probeValidity.has_value())
                {
                    throw ArgError("nothing to change: pass at least one of probes, cascadeBounds, probeValidity");
                }

                auto& dispatcher = events::EventDispatcher::instance();
                if (probes.has_value())
                {
                    events::render::gi::SetGIDebugProbesCommand command;
                    command.show = *probes;
                    dispatcher.execute(command);
                }
                if (cascadeBounds.has_value())
                {
                    events::render::gi::SetGIDebugCascadeBoundsCommand command;
                    command.show = *cascadeBounds;
                    dispatcher.execute(command);
                }
                if (probeValidity.has_value())
                {
                    events::render::gi::SetGIDebugProbeValidityCommand command;
                    command.show = *probeValidity;
                    dispatcher.execute(command);
                }

                const env::GISettings live = liveGi();
                const bool active = giActive(live);
                std::vector<std::string> warnings;
                if (!active)
                {
                    warnings.push_back("Debug views draw only while GI is active at Medium or higher (gi_set); the "
                                       "flags apply once it is");
                }
                nlohmann::json out{
                    {"debug", giDebugJson(live)},
                    {"active", active},
                    {"note", "Debug views are session-only: not saved and not undoable."},
                    {"warnings", warnings}
                };
                const auto onOff = [](bool value) { return value ? "on" : "off"; };
                const std::string summary = std::string("GI debug views: probes ") + onOff(live.showProbes) +
                                            ", cascade bounds " + onOff(live.showCascadeBounds) + ", probe validity " +
                                            onOff(live.showProbeValidity) + ".";
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // fog_get / fog_set
        // ------------------------------------------------------------------

        void registerFogGet(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "fog_get";
            tool.title = "Get fog";
            tool.description =
                "Read the volumetric fog (distance and height fog in one volume). 'authored' = the scene's saved fog "
                "(fog_set changes it), 'effective' = what renders now (while weather is enabled it overrides "
                "uniformDensity, heightFogDensity and fogColor), weather {enabled, overrides}, derived "
                "{extinctionAtGround (per metre), visibilityMeters (where the fog hides 95% of the light; null when no "
                "fog renders), limitedByMaxDistance and note when they apply} from the effective values, notes on how "
                "the settings map to looks (height fog, light shafts), ranges = every key with its range "
                "(includeRanges:false omits them).";
            tool.inputSchema = closedObject({
                {"includeRanges", schema::boolean("Include 'ranges' (the valid fog_set values). Default true.")}
            });
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"includeRanges"}, "fog_get");
                const ArgReader reader(args);
                const bool includeRanges = reader.optBool("includeRanges", true);

                const env::FogSettings authored = authoredSettings().postProcess.volumetricFog;
                const env::FogSettings effective = liveFog();
                const bool weatherOn = env::isWeatherEnabled();
                std::vector<std::string> warnings;
                if (!authored.enabled)
                {
                    warnings.push_back("Volumetric fog is disabled: nothing renders until fog_set {enabled:true}");
                }

                nlohmann::json overrides = nlohmann::json::array();
                if (weatherOn)
                {
                    for (const std::string& key : env::weatherDrivenKeys(env::fogTable()))
                    {
                        overrides.push_back(key);
                    }
                }
                const nlohmann::json derived = derivedFogJson(effective);
                nlohmann::json out{
                    {"authored", env::toJson(env::fogTable(), authored)},
                    {"effective", env::toJson(env::fogTable(), effective)},
                    {"weather", {{"enabled", weatherOn}, {"overrides", std::move(overrides)}}},
                    {"derived", derived},
                    {"notes", fogNotesJson()},
                    {"warnings", warnings}
                };
                if (includeRanges)
                {
                    out["ranges"] = env::rangesJson(env::fogTable());
                }
                const std::string summary = std::string("Volumetric fog ") + (effective.enabled ? "enabled" : "disabled") +
                                            ", visibility " + visibilityText(derived) +
                                            (weatherOn ? ", weather overrides the density and colour." : ".");
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }

        void registerFogSet(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "fog_set";
            tool.title = "Set fog";
            tool.description =
                "Change the volumetric fog: pass any fog_get key at the top level (partial update; an unknown key, "
                "a wrong type or an out-of-range value fails the whole call and changes nothing). There is no "
                "separate distance fog: uniformDensity is homogeneous fog within maxDistance; height fog adds "
                "heightFogDensity at and below heightFogOffset, thinning by heightFogFalloff per metre above it. "
                "Light shafts appear where geometry shadows the sun's light inside the fog (needs a directional light "
                "that casts shadows): raise scatteringCoefficient for stronger shafts; anisotropy 0.6-0.9 makes them "
                "brightest when looking toward the sun. Nothing renders unless enabled is true. While weather is "
                "enabled it overrides uniformDensity, heightFogDensity and fogColor live (the saved values still "
                "change). Saved with the scene on scene_save; one undo step; refused in Play mode. Returns {changed, "
                "diff, effective, derived, warnings}.";
            tool.inputSchema = env::schemaJson(env::fogTable(), "Volumetric fog settings to change (partial).");
            tool.timeout = fogTimeout;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                if (auto blocked = env::editBlocker("change the fog"))
                {
                    return ToolResult::error(*blocked);
                }
                if (!args.is_object() || args.empty())
                {
                    throw ArgError("nothing to change: pass at least one fog setting (fog_get lists them with their "
                                   "ranges)");
                }

                const env::FogSettings before = authoredSettings().postProcess.volumetricFog;
                const env::FogSettings staged = env::strictPatch(env::fogTable(), before, args, "");
                const nlohmann::json diff = env::diffJson(env::fogTable(), before, staged);
                const bool weatherOn = env::isWeatherEnabled();

                std::vector<std::string> warnings;
                if (!diff.empty())
                {
                    undo::UndoRecorder recorder("MCP: Set fog");
                    try
                    {
                        writeRecorded(recorder, before, staged);
                    }
                    catch (const std::exception& e)
                    {
                        recorder.rollback();
                        return ToolResult::error(std::string("The fog was not changed (rolled back): ") + e.what());
                    }
                    recorder.push();

                    if (weatherOn)
                    {
                        // The write replaced the live fog; let weather re-apply its overrides now so 'effective'
                        // shows what renders.
                        try
                        {
                            events::weather::UpdateWeatherCommand update;
                            update.deltaTime = 0.0f;
                            events::EventDispatcher::instance().execute(update);
                        }
                        catch (const std::exception& e)
                        {
                            warnings.push_back(std::string("Re-applying the weather failed: ") + e.what());
                        }
                    }
                }

                if (weatherOn)
                {
                    // Only keys that actually changed: an echoed fog_get object must not warn about every key.
                    const std::vector<std::string> driven = weatherDrivenIn(diff, env::weatherDrivenKeys(env::fogTable()), "");
                    if (!driven.empty())
                    {
                        warnings.push_back(weatherOverrideWarning(driven));
                    }
                }
                if (!staged.enabled)
                {
                    warnings.push_back("Volumetric fog is disabled: these settings are saved but nothing renders until "
                                       "enabled is true");
                }

                const env::FogSettings effective = liveFog();
                const nlohmann::json derived = derivedFogJson(effective);
                nlohmann::json out{
                    {"changed", !diff.empty()},
                    {"diff", diff},
                    {"effective", env::toJson(env::fogTable(), effective)},
                    {"derived", derived},
                    {"warnings", warnings}
                };
                const std::string summary = diff.empty()
                    ? std::string("Nothing changed.")
                    : "Fog updated (" + std::to_string(diff.size()) + " keys); visibility " + visibilityText(derived) + ".";
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // weather_get / weather_set
        // ------------------------------------------------------------------

        void registerWeatherGet(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "weather_get";
            tool.title = "Get weather";
            tool.description =
                "Read the session weather: enabled, state (cloud coverage / density / type, precipitation, wind, fog "
                "densities, atmosphere tint, ambient multiplier), matchesPreset (the preset the state equals, or "
                "null), transitioning / progress, presets (the names weather_set accepts), effectiveFog (the live fog "
                "weather drives), scheduleEnabled. Weather is session-only: it is not saved with the scene.";
            tool.inputSchema = closedObject(nlohmann::json::object());
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {}, "weather_get");
                const env::WeatherSnapshot session = env::readWeather();
                const float progress = transitionProgress();
                const bool scheduleEnabled =
                    events::EventDispatcher::instance().query(events::weather::IsWeatherScheduleEnabledQuery{});
                const env::FogSettings fog = liveFog();

                std::vector<std::string> warnings;
                if (session.enabled && !fog.enabled)
                {
                    warnings.push_back("Volumetric fog is disabled, so the weather's fog is invisible (weather_set "
                                       "{enableVolumetricFog:true} or fog_set {enabled:true})");
                }
                const nlohmann::json preset = matchingPreset(session.state);
                nlohmann::json out{
                    {"enabled", session.enabled},
                    {"state", weatherStateJson(session.state)},
                    {"matchesPreset", preset},
                    {"transitioning", progress < 1.0f},
                    {"progress", env::floatJson(progress)},
                    {"presets", weatherPresetNames()},
                    {"effectiveFog", effectiveFogJson(fog)},
                    {"scheduleEnabled", scheduleEnabled},
                    {"note", "session-only: weather is not saved with the scene"},
                    {"warnings", warnings}
                };
                const std::string summary = std::string("Weather ") + (session.enabled ? "on" : "off") +
                                            (preset.is_string() ? " (" + preset.get<std::string>() + ")" : "") + ".";
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }

        void registerWeatherSet(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "weather_set";
            tool.title = "Set weather";
            tool.description =
                "Change the session weather. preset (Clear, Cloudy, Overcast, LightRain, HeavyRain, Thunderstorm, "
                "LightSnow, HeavySnow, Fog, Sandstorm; case-insensitive) turns weather on and applies it immediately, "
                "or over transitionSeconds (0-120). enabled:false turns weather off and restores the scene's own fog "
                "and clouds. While enabled, weather drives the live fog density / colour, the clouds and the sky's sun "
                "irradiance and ambient. Its fog shows only through volumetric fog: enableVolumetricFog:true switches "
                "that on (saved with the scene). Weather itself is session-only (NOT saved with the scene); one undo "
                "step; refused in Play mode. Returns {changed, enabled, state, preset, transitioning, progress, "
                "effectiveFog, warnings}.";
            tool.inputSchema = closedObject({
                {"enabled", schema::boolean("Turn weather on or off. A preset turns it on.")},
                {"preset", schema::enumString("Weather preset to apply.", weatherPresetNames())},
                {"transitionSeconds", schema::numberRange("With preset: blend to it over this many seconds. Default 0 "
                                                          "(immediate).", 0.0, maxTransitionSeconds)},
                {"enableVolumetricFog", schema::boolean("Switch the scene's volumetric fog on (true) or off (false) "
                                                        "so the weather's fog shows. Saved with the scene.")}
            });
            tool.timeout = weatherTimeout;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                if (auto blocked = env::editBlocker("change the weather"))
                {
                    return ToolResult::error(*blocked);
                }
                env::rejectUnknownArgs(args, {"enabled", "preset", "transitionSeconds", "enableVolumetricFog"},
                                       "weather_set");
                const ArgReader reader(args);
                std::optional<bool> enabled;
                if (reader.has("enabled"))
                {
                    enabled = env::requireBool(reader.raw("enabled"), "enabled");
                }
                std::optional<::weather::WeatherPresetId> preset;
                if (reader.has("preset"))
                {
                    preset = ::weather::stringToPresetId(reader.requireEnum("preset", weatherPresetNames()));
                }
                if (preset.has_value() && enabled.has_value() && !*enabled)
                {
                    throw ArgError("a preset turns weather on: it cannot be combined with enabled:false");
                }
                float transitionSeconds = 0.0f;
                if (reader.has("transitionSeconds"))
                {
                    if (!preset.has_value())
                    {
                        throw ArgError("argument 'transitionSeconds' needs 'preset'");
                    }
                    transitionSeconds = env::requireFloat(reader.raw("transitionSeconds"), 0.0, maxTransitionSeconds,
                                                          "transitionSeconds");
                }
                std::optional<bool> enableFog;
                if (reader.has("enableVolumetricFog"))
                {
                    enableFog = env::requireBool(reader.raw("enableVolumetricFog"), "enableVolumetricFog");
                }
                if (!enabled.has_value() && !preset.has_value() && !enableFog.has_value())
                {
                    throw ArgError("nothing to change: pass at least one of enabled, preset, enableVolumetricFog");
                }

                const env::WeatherSnapshot before = env::readWeather();
                const bool transitioningBefore = transitionProgress() < 1.0f;
                const bool targetEnabled = preset.has_value() ? true : enabled.value_or(before.enabled);

                const env::FogSettings fogBefore = authoredSettings().postProcess.volumetricFog;
                env::FogSettings fog = fogBefore;
                if (enableFog.has_value())
                {
                    fog.enabled = *enableFog;
                }
                const bool fogChange = fog.enabled != fogBefore.enabled;
                const bool enabledChange = targetEnabled != before.enabled;
                const bool stateChange = preset.has_value() &&
                    (!env::sameWeatherState(before.state, ::weather::getPreset(*preset), 0.0f) || transitioningBefore);
                const bool changed = fogChange || enabledChange || stateChange;

                if (changed)
                {
                    // The end state, known up front: turning weather off resets its state to the defaults.
                    env::WeatherSnapshot after;
                    after.enabled = targetEnabled;
                    after.state = preset.has_value() ? ::weather::getPreset(*preset)
                                                     : (targetEnabled ? before.state : ::weather::WeatherState{});

                    undo::UndoRecorder recorder(preset.has_value()
                        ? "MCP: Set weather (" + std::string(::weather::presetIdToString(*preset)) + ")"
                        : std::string("MCP: Set weather"));
                    try
                    {
                        auto& dispatcher = events::EventDispatcher::instance();
                        if (fogChange)
                        {
                            writeRecorded(recorder, fogBefore, fog);
                        }
                        if (enabledChange || stateChange)
                        {
                            recorder.add(std::make_unique<undo::WeatherUndo>(before, after));

                            events::weather::SetWeatherEnabledCommand enable;
                            enable.enabled = targetEnabled;
                            dispatcher.execute(enable);
                            if (preset.has_value())
                            {
                                if (transitionSeconds > 0.0f)
                                {
                                    events::weather::SetWeatherPresetCommand transition;
                                    transition.preset = *preset;
                                    transition.transitionDuration = transitionSeconds;
                                    dispatcher.execute(transition);
                                }
                                else
                                {
                                    events::weather::SetWeatherImmediateCommand immediate;
                                    immediate.state = ::weather::getPreset(*preset);
                                    dispatcher.execute(immediate);
                                }
                            }
                        }
                        if (targetEnabled)
                        {
                            // Apply the weather to the live fog, clouds and sky now rather than next frame.
                            events::weather::UpdateWeatherCommand update;
                            update.deltaTime = 0.0f;
                            dispatcher.execute(update);
                        }
                    }
                    catch (const std::exception& e)
                    {
                        recorder.rollback();
                        return ToolResult::error(std::string("The weather was not changed (rolled back): ") + e.what());
                    }
                    recorder.push();
                }

                const env::WeatherSnapshot now = env::readWeather();
                const float progress = transitionProgress();
                const bool scheduleEnabled =
                    events::EventDispatcher::instance().query(events::weather::IsWeatherScheduleEnabledQuery{});
                std::vector<std::string> warnings;
                if (now.enabled && !fog.enabled)
                {
                    warnings.push_back("The weather's fog is invisible until volumetric fog is enabled (pass "
                                       "enableVolumetricFog:true or use fog_set)");
                }
                if (now.enabled && scheduleEnabled)
                {
                    warnings.push_back("The weather schedule is enabled and may override this");
                }

                nlohmann::json out{
                    {"changed", changed},
                    {"enabled", now.enabled},
                    {"state", weatherStateJson(now.state)},
                    {"preset", preset.has_value() ? nlohmann::json(::weather::presetIdToString(*preset))
                                                  : nlohmann::json(nullptr)},
                    {"transitioning", progress < 1.0f},
                    {"progress", env::floatJson(progress)},
                    {"effectiveFog", effectiveFogJson(liveFog())},
                    {"note", "session-only: weather is not saved with the scene"},
                    {"warnings", warnings}
                };
                std::string summary = !changed ? std::string("Nothing changed.")
                                               : std::string("Weather ") + (now.enabled ? "on" : "off");
                if (changed && preset.has_value())
                {
                    summary += std::string(": ") + ::weather::presetIdToString(*preset) +
                               (transitionSeconds > 0.0f ? " over " + numberText(transitionSeconds) + " s" : "");
                }
                if (changed)
                {
                    summary += fogChange ? (fog.enabled ? "; volumetric fog on." : "; volumetric fog off.") : ".";
                }
                return reply(std::move(out), std::move(summary));
            };
            registry.add(std::move(tool));
        }
    }

    void registerEnvironmentTools(ToolRegistry& registry, const ToolContext&)
    {
        registerEnvironmentGet(registry);
        registerEnvironmentSet(registry);
        registerIblSet(registry);
        registerIblBake(registry);
        registerGiGet(registry);
        registerGiSet(registry);
        registerGiDebug(registry);
        registerFogGet(registry);
        registerFogSet(registry);
        registerWeatherGet(registry);
        registerWeatherSet(registry);
    }
}

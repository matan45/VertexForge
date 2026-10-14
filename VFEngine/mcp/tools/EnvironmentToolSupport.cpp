#include "EnvironmentToolSupport.hpp"
#include "PathSandbox.hpp"
#include "ToolHelpers.hpp"

#include "events/EventDispatcher.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/render/AtmosphereEvents.hpp"
#include "events/render/CloudEvents.hpp"
#include "events/render/GIEvents.hpp"
#include "events/render/PostProcessEvents.hpp"
#include "events/render/RenderEvents.hpp"
#include "events/scene/ComponentMediaEvents.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "events/scene/SceneSettingsEvents.hpp"
#include "events/weather/WeatherEvents.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <type_traits>

namespace mcp::tools::env
{
    namespace
    {
        // Passed for the keys weather overrides live while it is enabled.
        constexpr bool weatherDriven = true;

        // ------------------------------------------------------------------
        // Text
        // ------------------------------------------------------------------

        // JSON spelling of the shortest round-trip float: 1e-4 stays "0.0001" rather than
        // std::to_string's "0.000100".
        std::string floatText(float value)
        {
            return floatJson(value).dump();
        }

        // Agent input echoed in an error; replace: it may hold invalid UTF-8.
        std::string valueText(const nlohmann::json& value)
        {
            return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }

        std::string pathOf(std::string_view prefix, std::string_view key)
        {
            return prefix.empty() ? std::string(key) : std::string(prefix) + "." + std::string(key);
        }

        bool equalsIgnoreCase(std::string_view a, std::string_view b)
        {
            return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y)
            {
                return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
            });
        }

        template <typename Range>
        std::string joined(const Range& names)
        {
            std::string out;
            for (const auto& name : names)
            {
                out += out.empty() ? "" : ", ";
                out += name;
            }
            return out.empty() ? std::string("none") : out;
        }

        // The bounds as the floats a value is compared against (requireFloat narrows first), so
        // moonAzimuth's max reads 359.99997 rather than its double expansion.
        std::string rangeError(std::string_view path, double min, double max, const nlohmann::json& value)
        {
            return "'" + std::string(path) + "' must be between " + floatText(static_cast<float>(min)) + " and " +
                   floatText(static_cast<float>(max)) + " (got " + valueText(value) + ")";
        }

        // ------------------------------------------------------------------
        // Validation: every check throws ArgError naming `path`
        // ------------------------------------------------------------------

        int64_t checkedInteger(const nlohmann::json& value, double min, double max, std::string_view path)
        {
            double number = 0.0;
            if (value.is_number_unsigned())
            {
                number = static_cast<double>(value.get<uint64_t>());
            }
            else if (value.is_number_integer())
            {
                number = static_cast<double>(value.get<int64_t>());
            }
            else if (value.is_number_float() && std::isfinite(value.get<double>()) &&
                     value.get<double>() == std::trunc(value.get<double>()))
            {
                number = value.get<double>(); // models frequently send 3.0 for 3
            }
            else
            {
                throw ArgError("'" + std::string(path) + "' must be an integer (got " + valueText(value) + ")");
            }
            if (number < min || number > max)
            {
                throw ArgError("'" + std::string(path) + "' must be between " + std::to_string(static_cast<int64_t>(min)) +
                               " and " + std::to_string(static_cast<int64_t>(max)) + " (got " + valueText(value) + ")");
            }
            return static_cast<int64_t>(number);
        }

        std::string_view checkedEnum(const nlohmann::json& value, std::span<const std::string_view> names,
                                     std::string_view path)
        {
            if (value.is_string())
            {
                const std::string text = value.get<std::string>();
                for (std::string_view name : names)
                {
                    if (equalsIgnoreCase(text, name))
                    {
                        return name;
                    }
                }
            }
            throw ArgError("'" + std::string(path) + "' must be one of " + joined(names) + " (got " + valueText(value) + ")");
        }

        // The value in the form FieldDesc::set takes (the form FieldDesc::get reports).
        nlohmann::json canonicalValue(FieldKind kind, double min, double max, std::span<const std::string_view> names,
                                      const nlohmann::json& value, std::string_view path)
        {
            switch (kind)
            {
            case FieldKind::Bool:
                return requireBool(value, path);
            case FieldKind::Float:
                return static_cast<double>(requireFloat(value, min, max, path));
            case FieldKind::Int:
            case FieldKind::UInt:
                return checkedInteger(value, min, max, path);
            case FieldKind::Vec3:
            {
                const glm::vec3 vector = requireVec3(value, min, max, path);
                return nlohmann::json::array({static_cast<double>(vector.x), static_cast<double>(vector.y),
                                              static_cast<double>(vector.z)});
            }
            case FieldKind::Enum:
                return std::string(checkedEnum(value, names, path));
            }
            throw std::logic_error("unknown field kind");
        }

        // ------------------------------------------------------------------
        // Reporting
        // ------------------------------------------------------------------

        nlohmann::json namesJson(std::span<const std::string_view> names)
        {
            nlohmann::json out = nlohmann::json::array();
            for (std::string_view name : names)
            {
                out.push_back(std::string(name));
            }
            return out;
        }

        nlohmann::json rangeEntry(FieldKind kind, double min, double max, std::span<const std::string_view> names)
        {
            switch (kind)
            {
            case FieldKind::Bool:
                return {{"type", "boolean"}};
            // Float bounds are reported as the floats values are compared against (see rangeError).
            case FieldKind::Float:
                return {{"type", "number"}, {"min", floatJson(static_cast<float>(min))},
                        {"max", floatJson(static_cast<float>(max))}};
            case FieldKind::Int:
            case FieldKind::UInt:
                return {{"type", "integer"}, {"min", static_cast<int64_t>(min)}, {"max", static_cast<int64_t>(max)}};
            case FieldKind::Vec3:
                return {{"type", "vec3"}, {"min", floatJson(static_cast<float>(min))},
                        {"max", floatJson(static_cast<float>(max))}};
            case FieldKind::Enum:
                return {{"type", "enum"}, {"values", namesJson(names)}};
            }
            throw std::logic_error("unknown field kind");
        }

        nlohmann::json fieldSchema(FieldKind kind, double min, double max, std::span<const std::string_view> names,
                                   std::string description)
        {
            switch (kind)
            {
            case FieldKind::Bool:
                return schema::boolean(std::move(description));
            case FieldKind::Float:
                return schema::numberRange(std::move(description), min, max);
            case FieldKind::Int:
            case FieldKind::UInt:
                return schema::integerRange(std::move(description), static_cast<int64_t>(min), static_cast<int64_t>(max));
            case FieldKind::Vec3:
            {
                nlohmann::json out = schema::vec3(std::move(description));
                out["items"] = {{"type", "number"}, {"minimum", min}, {"maximum", max}};
                return out;
            }
            case FieldKind::Enum:
            {
                std::vector<std::string> values;
                for (std::string_view name : names)
                {
                    values.emplace_back(name);
                }
                return schema::enumString(std::move(description), values);
            }
            }
            throw std::logic_error("unknown field kind");
        }

        template <typename T>
        const FieldDesc<T>* findField(const SectionTable<T>& table, std::string_view key)
        {
            for (const FieldDesc<T>& field : table.fields)
            {
                if (field.key == key)
                {
                    return &field;
                }
            }
            return nullptr;
        }

        // The keys a patch may set.
        template <typename T>
        std::string settableKeys(const SectionTable<T>& table)
        {
            std::vector<std::string_view> keys;
            for (const FieldDesc<T>& field : table.fields)
            {
                if (field.persisted && field.setHint.empty())
                {
                    keys.push_back(field.key);
                }
            }
            return joined(keys);
        }

        // True when `value` names exactly what `staged` already holds for `field`, compared in the
        // reported form: the value as given, or canonicalised and written to a probe copy (so 12 and
        // 12.0, or "medium" and "Medium", match). An invalid value never matches.
        template <typename T>
        bool holdsValue(const FieldDesc<T>& field, const T& staged, const nlohmann::json& value, std::string_view path)
        {
            const nlohmann::json current = field.get(staged);
            if (value == current)
            {
                return true;
            }
            T probe = staged;
            try
            {
                field.set(probe, canonicalValue(field.kind, field.min, field.max, field.enumNames, value, path));
            }
            catch (const ArgError&)
            {
                return false;
            }
            return field.get(probe) == current;
        }

        // ------------------------------------------------------------------
        // Field builders: captureless, so get / set decay to plain function pointers
        // ------------------------------------------------------------------

        template <typename E>
        constexpr std::span<const std::string_view> enumNamesOf()
        {
            if constexpr (std::is_same_v<E, ::postprocess::VolumetricQuality>)
            {
                return fogQualityNames;
            }
            else
            {
                static_assert(std::is_same_v<E, ::render::gi::GIQuality>, "no canonical names for this enum");
                return giQualityNames;
            }
        }

        template <typename T, bool T::*Member>
        FieldDesc<T> boolField(std::string_view key, std::string_view help)
        {
            FieldDesc<T> field;
            field.key = key;
            field.kind = FieldKind::Bool;
            field.help = help;
            field.get = [](const T& value) -> nlohmann::json { return value.*Member; };
            field.set = [](T& value, const nlohmann::json& v) { value.*Member = v.get<bool>(); };
            return field;
        }

        template <typename T, float T::*Member>
        FieldDesc<T> floatField(std::string_view key, double min, double max, std::string_view help, bool weather = false)
        {
            FieldDesc<T> field;
            field.key = key;
            field.kind = FieldKind::Float;
            field.min = min;
            field.max = max;
            field.weatherDriven = weather;
            field.help = help;
            field.get = [](const T& value) -> nlohmann::json { return floatJson(value.*Member); };
            field.set = [](T& value, const nlohmann::json& v) { value.*Member = static_cast<float>(v.get<double>()); };
            return field;
        }

        template <typename T, int T::*Member>
        FieldDesc<T> intField(std::string_view key, double min, double max, std::string_view help)
        {
            FieldDesc<T> field;
            field.key = key;
            field.kind = FieldKind::Int;
            field.min = min;
            field.max = max;
            field.help = help;
            field.get = [](const T& value) -> nlohmann::json { return static_cast<int64_t>(value.*Member); };
            field.set = [](T& value, const nlohmann::json& v) { value.*Member = static_cast<int>(v.get<int64_t>()); };
            return field;
        }

        template <typename T, uint32_t T::*Member>
        FieldDesc<T> uintField(std::string_view key, double min, double max, std::string_view help)
        {
            FieldDesc<T> field;
            field.key = key;
            field.kind = FieldKind::UInt;
            field.min = min;
            field.max = max;
            field.help = help;
            field.get = [](const T& value) -> nlohmann::json { return static_cast<int64_t>(value.*Member); };
            field.set = [](T& value, const nlohmann::json& v) { value.*Member = static_cast<uint32_t>(v.get<int64_t>()); };
            return field;
        }

        template <typename T, glm::vec3 T::*Member>
        FieldDesc<T> vec3Field(std::string_view key, double min, double max, std::string_view help, bool weather = false)
        {
            FieldDesc<T> field;
            field.key = key;
            field.kind = FieldKind::Vec3;
            field.min = min;
            field.max = max;
            field.weatherDriven = weather;
            field.help = help;
            field.get = [](const T& value) -> nlohmann::json { return vec3Json(value.*Member); };
            field.set = [](T& value, const nlohmann::json& v)
            {
                value.*Member = glm::vec3(static_cast<float>(v.at(0).get<double>()), static_cast<float>(v.at(1).get<double>()),
                                          static_cast<float>(v.at(2).get<double>()));
            };
            return field;
        }

        // A float[3] member (VolumetricFogSettings::fogColor).
        template <typename T, float (T::*Member)[3]>
        FieldDesc<T> rgbArrayField(std::string_view key, double min, double max, std::string_view help, bool weather = false)
        {
            FieldDesc<T> field;
            field.key = key;
            field.kind = FieldKind::Vec3;
            field.min = min;
            field.max = max;
            field.weatherDriven = weather;
            field.help = help;
            field.get = [](const T& value) -> nlohmann::json
            {
                const float (&rgb)[3] = value.*Member;
                return nlohmann::json::array({floatJson(rgb[0]), floatJson(rgb[1]), floatJson(rgb[2])});
            };
            field.set = [](T& value, const nlohmann::json& v)
            {
                float (&rgb)[3] = value.*Member;
                for (std::size_t i = 0; i < 3; ++i)
                {
                    rgb[i] = static_cast<float>(v.at(i).get<double>());
                }
            };
            return field;
        }

        template <typename T, typename E, E T::*Member>
        FieldDesc<T> enumField(std::string_view key, std::string_view help)
        {
            FieldDesc<T> field;
            field.key = key;
            field.kind = FieldKind::Enum;
            field.enumNames = enumNamesOf<E>();
            field.help = help;
            field.get = [](const T& value) -> nlohmann::json
            {
                const std::span<const std::string_view> names = enumNamesOf<E>();
                const auto index = static_cast<std::size_t>(value.*Member);
                return index < names.size() ? nlohmann::json(std::string(names[index])) : nlohmann::json(index);
            };
            field.set = [](T& value, const nlohmann::json& v)
            {
                const std::span<const std::string_view> names = enumNamesOf<E>();
                const std::string name = v.get<std::string>();
                for (std::size_t i = 0; i < names.size(); ++i)
                {
                    if (names[i] == name)
                    {
                        value.*Member = static_cast<E>(i);
                        return;
                    }
                }
                throw std::logic_error("'" + name + "' is not a canonical enum name");
            };
            return field;
        }

        // A key reported by *_get that a patch may not set; `hint` names what sets it instead.
        template <typename T>
        FieldDesc<T> readOnly(FieldDesc<T> field, std::string_view hint)
        {
            field.setHint = hint;
            return field;
        }

        // Live-only state: never part of the section's JSON, refused in a patch with `hint`.
        template <typename T>
        FieldDesc<T> liveOnly(FieldDesc<T> field, std::string_view hint)
        {
            field.setHint = hint;
            field.persisted = false;
            return field;
        }

        // ------------------------------------------------------------------
        // Cross-field checks
        // ------------------------------------------------------------------

        std::string checkAtmosphere(const AtmosphereSettings& value)
        {
            if (value.atmosphereRadius > value.planetRadius)
            {
                return {};
            }
            return "atmosphereRadius (" + floatText(value.atmosphereRadius) + ") must be greater than planetRadius (" +
                   floatText(value.planetRadius) + ")";
        }

        std::string checkClouds(const CloudSettings& value)
        {
            if (value.cloudMinAltitude < value.cloudMaxAltitude)
            {
                return {};
            }
            return "cloudMinAltitude (" + floatText(value.cloudMinAltitude) + ") must be below cloudMaxAltitude (" +
                   floatText(value.cloudMaxAltitude) + ")";
        }

        void publishChanged(events::scene::RenderSettingsSection section)
        {
            events::scene::RenderSettingsChangedNotification changed;
            changed.section = section;
            changed.sceneLoad = false;
            events::EventDispatcher::instance().publish(changed);
        }

        std::string engineFileText(const std::string& enginePath)
        {
            // Engine paths are narrow (ANSI) strings; report them as UTF-8.
            return pathToUtf8(std::filesystem::path(enginePath));
        }
    }

    // ------------------------------------------------------------------
    // Tables. Ranges: the scene loader's clamp where it has one (SceneSerializeEnvironment.cpp,
    // SceneSerializePostProcess.cpp, SceneSerializeRender.cpp), otherwise the editor window's slider
    // (AtmosphereConfigWindow, CloudConfigWindow, GIConfigWindow), so every accepted value survives a
    // save + load unchanged.
    // ------------------------------------------------------------------

    const SectionTable<AtmosphereSettings>& atmosphereTable()
    {
        using S = AtmosphereSettings;
        static const SectionTable<S> table{
            "atmosphere",
            {
                boolField<S, &S::enabled>("enabled",
                    "Draw the procedural sky (atmospheric scattering). false: the HDR skybox (ibl_set), if any, is the sky."),
                floatField<S, &S::planetRadius>("planetRadius", 1.0e5, 1.0e8, "Planet radius in metres (Earth 6360000)."),
                floatField<S, &S::atmosphereRadius>("atmosphereRadius", 1.0e5, 1.0e8,
                    "Outer radius of the atmosphere in metres, greater than planetRadius (Earth 6460000)."),
                vec3Field<S, &S::rayleighScattering>("rayleighScattering", 0.0, 1.0e-4,
                    "Per-metre RGB scattering of the air: the blue sky (Earth [5.802e-6, 1.3558e-5, 3.31e-5])."),
                floatField<S, &S::rayleighDensityExpScale>("rayleighDensityExpScale", -0.01, -2.0e-5,
                    "-1 / air scale height in metres (Earth -1/8000 = -0.000125)."),
                floatField<S, &S::mieScattering>("mieScattering", 0.0, 1.0e-4,
                    "Per-metre aerosol scattering: haze and the glow around the sun (Earth 3.996e-6)."),
                floatField<S, &S::mieAbsorption>("mieAbsorption", 0.0, 1.0e-4, "Per-metre aerosol absorption (Earth 4.4e-6)."),
                floatField<S, &S::mieAnisotropy>("mieAnisotropy", -1.0, 1.0,
                    "Aerosol phase g; 0.8 = strong forward glow around the sun."),
                floatField<S, &S::mieDensityExpScale>("mieDensityExpScale", -0.02, -5.0e-5,
                    "-1 / aerosol scale height in metres (Earth -1/1200)."),
                vec3Field<S, &S::ozoneAbsorption>("ozoneAbsorption", 0.0, 1.0e-5,
                    "Per-metre RGB ozone absorption: warmer sunrise and sunset colours."),
                floatField<S, &S::ozoneCenterAlt>("ozoneCenterAlt", 0.0, 1.0e5, "Altitude of the ozone layer's centre, metres."),
                floatField<S, &S::ozoneWidth>("ozoneWidth", 1000.0, 5.0e4, "Thickness of the ozone layer, metres."),
                vec3Field<S, &S::sunIrradiance>("sunIrradiance", 0.0, 10.0,
                    "RGB sun irradiance: brightness and colour of the sky and sun disc.", weatherDriven),
                floatField<S, &S::sunAngularRadius>("sunAngularRadius", 1.745e-4, 0.08727,
                    "Sun disc radius in radians (0.00935 = the real sun)."),
                readOnly(floatField<S, &S::sunAzimuth>("sunAzimuth", 0.0, 360.0, "Sun azimuth in degrees."),
                         "set it with sun.azimuth or timeOfDay"),
                readOnly(floatField<S, &S::sunElevation>("sunElevation", -90.0, 90.0, "Sun elevation in degrees."),
                         "set it with sun.elevation or timeOfDay"),
                // The loader stores fmod(v, 360): exact below 360, so the bound is the largest float below 360
                // (dayNightAngles produces any value in [0, 360), and every one of them must round-trip).
                floatField<S, &S::moonAzimuth>("moonAzimuth", 0.0, static_cast<double>(std::nextafter(360.0f, 0.0f)),
                    "Moon azimuth in degrees, below 360."),
                floatField<S, &S::moonElevation>("moonElevation", -90.0, 90.0, "Moon elevation in degrees."),
                floatField<S, &S::moonAngularRadius>("moonAngularRadius", 0.001, 0.08727, "Moon disc radius in radians."),
                floatField<S, &S::moonBrightness>("moonBrightness", 0.0, 1.0, "Moon brightness relative to the sun."),
                floatField<S, &S::starDensity>("starDensity", 0.0, 0.05, "Probability of a star per sky cell."),
                floatField<S, &S::starBrightness>("starBrightness", 0.0, 10.0, "Star brightness (HDR multiplier)."),
                floatField<S, &S::starTwinkleSpeed>("starTwinkleSpeed", 0.0, 5.0, "Star twinkle speed."),
                floatField<S, &S::nightSkyBrightness>("nightSkyBrightness", 0.0, 0.05, "Sky brightness floor at night."),
                boolField<S, &S::dayNightEnabled>("dayNightEnabled",
                    "Advance timeOfDay by cycleSpeed in Play mode (never in edit mode)."),
                readOnly(floatField<S, &S::timeOfDay>("timeOfDay", 0.0, 24.0, "Time of day in hours."),
                         "set it with the timeOfDay argument"),
                floatField<S, &S::cycleSpeed>("cycleSpeed", 0.0, 100.0,
                    "Day-night speed: game hours per real minute at 1; 0 = paused."),
                floatField<S, &S::moonPhaseOffset>("moonPhaseOffset", 0.0, 1.0,
                    "Shifts the moon's azimuth relative to the sun (0 = opposite the sun)."),
                boolField<S, &S::sunColorFromAtmosphere>("sunColorFromAtmosphere",
                    "Tint the sun light by the atmosphere: warm at sunrise and sunset, dim below the horizon."),
                floatField<S, &S::sunColorFeedbackStrength>("sunColorFeedbackStrength", 0.0, 1.0,
                    "0 = white sun light, 1 = full atmospheric tint."),
                boolField<S, &S::cycleControlsSunEntity>("cycleControlsSunEntity",
                    "In Play mode the day-night cycle rotates the sun light, so lighting and shadows follow the sky."),
                vec3Field<S, &S::groundAlbedo>("groundAlbedo", 0.0, 1.0, "RGB reflectance of the ground below the horizon."),
                floatField<S, &S::aerialMaxDist>("aerialMaxDist", 1000.0, 1.0e6,
                    "Aerial perspective (distance haze) range in metres."),
                floatField<S, &S::aerialIntensity>("aerialIntensity", 0.0, 5.0, "Aerial perspective strength.", weatherDriven),
                boolField<S, &S::dynamicAmbient>("dynamicAmbient",
                    "Capture the sky into the ambient light and reflections (replaces the HDR IBL's lighting); "
                    "needs enabled."),
                floatField<S, &S::ambientIntensity>("ambientIntensity", 0.0, 10.0,
                    "Multiplier on the captured sky ambient.", weatherDriven),
                intField<S, &S::ambientItemsPerFrame>("ambientItemsPerFrame", 1.0, 32.0,
                    "Sky capture work items per frame (42 make one full refresh)."),
            },
            &checkAtmosphere
        };
        return table;
    }

    const SectionTable<CloudSettings>& cloudTable()
    {
        using S = CloudSettings;
        static const SectionTable<S> table{
            "clouds",
            {
                boolField<S, &S::enabled>("enabled", "Draw the volumetric cloud layer."),
                floatField<S, &S::cloudMinAltitude>("cloudMinAltitude", 100.0, 1.0e4,
                    "Cloud layer base in metres above the ground, below cloudMaxAltitude."),
                floatField<S, &S::cloudMaxAltitude>("cloudMaxAltitude", 500.0, 1.5e4,
                    "Cloud layer top in metres above the ground."),
                floatField<S, &S::globalDensity>("globalDensity", 0.0, 1.0, "Cloud density.", weatherDriven),
                floatField<S, &S::globalCoverage>("globalCoverage", 0.0, 1.0, "Fraction of the sky covered.", weatherDriven),
                floatField<S, &S::cloudType>("cloudType", 0.0, 1.0, "0 = flat stratus .. 1 = puffy cumulus.", weatherDriven),
                floatField<S, &S::shapeScale>("shapeScale", 1.0e-4, 0.01, "Base noise frequency; smaller = larger clouds."),
                floatField<S, &S::detailScale>("detailScale", 0.001, 0.1, "Detail noise frequency."),
                floatField<S, &S::erosionStrength>("erosionStrength", 0.0, 1.0, "Edge erosion by the detail noise."),
                floatField<S, &S::curlStrength>("curlStrength", 0.0, 1.0, "Curl-noise distortion of the shapes."),
                floatField<S, &S::windSpeed>("windSpeed", 0.0, 100.0, "Wind speed in m/s.", weatherDriven),
                floatField<S, &S::windDirectionDeg>("windDirectionDeg", 0.0, 360.0, "Wind direction in degrees.", weatherDriven),
                vec3Field<S, &S::cloudColorTint>("cloudColorTint", 0.0, 2.0, "RGB multiplier on the cloud lighting.",
                                                 weatherDriven),
                floatField<S, &S::lightAbsorption>("lightAbsorption", 0.0, 2.0, "Light absorption inside the clouds."),
                floatField<S, &S::phaseForward>("phaseForward", 0.0, 0.999, "Forward scattering lobe g."),
                floatField<S, &S::phaseBackward>("phaseBackward", -0.999, 0.0, "Backward scattering lobe g."),
                floatField<S, &S::phaseBlend>("phaseBlend", 0.0, 1.0, "Mix of the forward and backward lobes."),
                floatField<S, &S::ambientIntensity>("ambientIntensity", 0.0, 2.0, "Ambient light inside the clouds.",
                                                    weatherDriven),
                floatField<S, &S::silverLiningIntensity>("silverLiningIntensity", 0.0, 2.0,
                    "Bright rim of thin cloud edges toward the sun."),
                floatField<S, &S::silverLiningSpread>("silverLiningSpread", 1.0, 20.0, "Silver lining falloff exponent."),
                floatField<S, &S::multiScatterBoost>("multiScatterBoost", 0.0, 2.0, "Multi-scattering brightening inside."),
                floatField<S, &S::temporalBlendFactor>("temporalBlendFactor", 0.0, 0.99, "Temporal reprojection blend."),
                uintField<S, &S::maxMarchSteps>("maxMarchSteps", 16.0, 256.0, "Ray-march steps (quality vs cost)."),
                uintField<S, &S::lightMarchSteps>("lightMarchSteps", 2.0, 16.0, "Light ray-march steps."),
            },
            &checkClouds
        };
        return table;
    }

    // earlyTerminationThreshold is deliberately absent: the scene file does not store it.
    const SectionTable<FogSettings>& fogTable()
    {
        using S = FogSettings;
        static const SectionTable<S> table{
            "volumetric fog",
            {
                boolField<S, &S::enabled>("enabled", "Render volumetric fog; nothing shows while false."),
                enumField<S, ::postprocess::VolumetricQuality, &S::quality>("quality", "Fog volume resolution."),
                floatField<S, &S::uniformDensity>("uniformDensity", 0.0, 1.0,
                    "Homogeneous fog everywhere within maxDistance (distance fog).", weatherDriven),
                rgbArrayField<S, &S::fogColor>("fogColor", 0.0, 1.0, "RGB fog colour (albedo).", weatherDriven),
                floatField<S, &S::heightFogDensity>("heightFogDensity", 0.0, 1.0,
                    "Extra fog density at and below heightFogOffset.", weatherDriven),
                floatField<S, &S::heightFogFalloff>("heightFogFalloff", 0.0, 5.0,
                    "Height fog thinning per metre above heightFogOffset (exponential); 0 = the same at every height."),
                floatField<S, &S::heightFogOffset>("heightFogOffset", -100.0, 100.0,
                    "World height in metres where the height fog starts to thin."),
                floatField<S, &S::scatteringCoefficient>("scatteringCoefficient", 0.0, 5.0,
                    "Light scattering: brighter, thicker fog and stronger sun light shafts."),
                floatField<S, &S::absorptionCoefficient>("absorptionCoefficient", 0.0, 5.0, "Light absorption: darker fog."),
                floatField<S, &S::anisotropy>("anisotropy", -0.99, 0.99,
                    "Phase g; 0.6-0.9 scatters forward: light shafts are brightest when looking toward the sun."),
                floatField<S, &S::temporalBlendFactor>("temporalBlendFactor", 0.0, 1.0, "Temporal reprojection blend."),
                floatField<S, &S::intensity>("intensity", 0.0, 5.0, "In-scattered light multiplier."),
                floatField<S, &S::ambientIntensity>("ambientIntensity", 0.0, 2.0, "Ambient light in the fog."),
                floatField<S, &S::maxDistance>("maxDistance", 10.0, 5000.0, "Metres from the camera the fog covers."),
                floatField<S, &S::giInjectionIntensity>("giInjectionIntensity", 0.0, 5.0,
                    "Global illumination injected into the fog."),
                boolField<S, &S::noiseEnabled>("noiseEnabled", "Break the fog up with drifting noise."),
                floatField<S, &S::noiseScale>("noiseScale", 0.001, 1.0, "Noise frequency per metre."),
                floatField<S, &S::noiseIntensity>("noiseIntensity", 0.0, 1.0, "Noise strength."),
                floatField<S, &S::noiseSpeed>("noiseSpeed", 0.0, 2.0, "Noise drift speed."),
                intField<S, &S::noiseOctaves>("noiseOctaves", 1.0, 4.0, "Noise octaves."),
            },
            nullptr
        };
        return table;
    }

    // The scene loader clamps only quality and ssgiSampleCount; the rest follow GIConfigWindow's sliders.
    const SectionTable<GISettings>& giTable()
    {
        using S = GISettings;
        static const SectionTable<S> table{
            "GI",
            {
                boolField<S, &S::enabled>("enabled", "Global illumination on / off."),
                enumField<S, ::render::gi::GIQuality, &S::quality>("quality",
                    "Probe cascades: Medium 1, High 3, Ultra 4; Off = no probes."),
                floatField<S, &S::probeSpacing>("probeSpacing", 1.0, 8.0, "Metres between cascade-0 probes."),
                floatField<S, &S::cascadeMultiplier>("cascadeMultiplier", 1.5, 4.0, "Probe spacing growth per cascade."),
                uintField<S, &S::probeRaysPerUpdate>("probeRaysPerUpdate", 16.0, 512.0, "Rays per probe update."),
                floatField<S, &S::temporalBlendFactor>("temporalBlendFactor", 0.5, 0.99,
                    "Probe history weight: higher = smoother but slower to react."),
                floatField<S, &S::probeUpdateRate>("probeUpdateRate", 0.05, 1.0, "Fraction of the probes updated per frame."),
                floatField<S, &S::maxProbeDistance>("maxProbeDistance", 50.0, 500.0, "How far a probe ray travels, metres."),
                boolField<S, &S::farFieldEnabled>("farFieldEnabled", "Extra coarse cascades for distant GI (open worlds)."),
                floatField<S, &S::farFieldMaxDistance>("farFieldMaxDistance", 100.0, 2000.0, "Far-field GI range, metres."),
                uintField<S, &S::farFieldCascadeCount>("farFieldCascadeCount", 1.0, 4.0, "Far-field cascades."),
                floatField<S, &S::farFieldProbeSpacing>("farFieldProbeSpacing", 1.0, 64.0, "Far-field probe spacing, metres."),
                uintField<S, &S::farFieldRaysPerUpdate>("farFieldRaysPerUpdate", 8.0, 256.0, "Rays per far-field probe update."),
                floatField<S, &S::farFieldUpdateRate>("farFieldUpdateRate", 0.05, 1.0,
                    "Fraction of the far-field probes updated per frame."),
                boolField<S, &S::ssgiEnabled>("ssgiEnabled", "Screen-space GI: short-range bounce light."),
                floatField<S, &S::ssgiIntensity>("ssgiIntensity", 0.0, 2.0, "Screen-space GI strength."),
                floatField<S, &S::ssgiRadius>("ssgiRadius", 0.5, 10.0, "Screen-space GI radius, metres."),
                floatField<S, &S::ssgiMaxDistance>("ssgiMaxDistance", 10.0, 500.0, "Screen-space GI range, metres."),
                intField<S, &S::ssgiSampleCount>("ssgiSampleCount", 4.0, 16.0, "Screen-space GI samples."),
                floatField<S, &S::ssgiTemporalBlend>("ssgiTemporalBlend", 0.05, 0.5, "Screen-space GI temporal blend."),
                boolField<S, &S::ssgiHalfResolution>("ssgiHalfResolution", "Trace screen-space GI at half resolution."),
                liveOnly(boolField<S, &S::showProbes>("showProbes", "Probe debug view."), "use gi_debug"),
                liveOnly(boolField<S, &S::showCascadeBounds>("showCascadeBounds", "Cascade bounds debug view."), "use gi_debug"),
                liveOnly(boolField<S, &S::showProbeValidity>("showProbeValidity", "Probe validity debug view."), "use gi_debug"),
            },
            nullptr
        };
        return table;
    }

    // ------------------------------------------------------------------
    // Generic ops
    // ------------------------------------------------------------------

    template <typename T>
    nlohmann::json toJson(const SectionTable<T>& table, const T& value)
    {
        nlohmann::json out = nlohmann::json::object();
        for (const FieldDesc<T>& field : table.fields)
        {
            if (field.persisted)
            {
                out[std::string(field.key)] = field.get(value);
            }
        }
        return out;
    }

    template <typename T>
    T strictPatch(const SectionTable<T>& table, const T& base, const nlohmann::json& patch, std::string_view prefix)
    {
        if (!patch.is_object())
        {
            throw ArgError(prefix.empty() ? std::string("the arguments must be an object")
                                          : "'" + std::string(prefix) + "' must be an object (got " + valueText(patch) + ")");
        }

        T staged = base;
        for (auto it = patch.begin(); it != patch.end(); ++it)
        {
            const FieldDesc<T>* field = findField(table, it.key());
            if (field == nullptr)
            {
                throw ArgError(prefix.empty()
                                   ? "unknown argument '" + it.key() + "'; valid arguments: " + settableKeys(table)
                                   : "unknown " + std::string(prefix) + " key '" + it.key() + "'; valid keys: " +
                                         settableKeys(table));
            }
            const std::string path = pathOf(prefix, field->key);
            if (!field->setHint.empty())
            {
                // Echoing the current value back (e.g. a whole environment_get object) is a no-op. Live-only
                // keys (the GI debug views) are never reported, so they are always refused with the hint.
                if (field->persisted && holdsValue(*field, staged, it.value(), path))
                {
                    continue;
                }
                throw ArgError("'" + path + "' is read-only here: " + std::string(field->setHint));
            }
            field->set(staged, canonicalValue(field->kind, field->min, field->max, field->enumNames, it.value(), path));
        }

        if (table.crossCheck != nullptr)
        {
            const std::string error = table.crossCheck(staged);
            if (!error.empty())
            {
                throw ArgError(prefix.empty() ? error : std::string(prefix) + ": " + error);
            }
        }
        return staged;
    }

    template <typename T>
    nlohmann::json rangesJson(const SectionTable<T>& table)
    {
        nlohmann::json out = nlohmann::json::object();
        for (const FieldDesc<T>& field : table.fields)
        {
            if (!field.persisted)
            {
                continue;
            }
            nlohmann::json entry = rangeEntry(field.kind, field.min, field.max, field.enumNames);
            if (field.weatherDriven)
            {
                entry["weatherDriven"] = true;
            }
            if (!field.setHint.empty())
            {
                entry["readOnly"] = std::string(field.setHint);
            }
            out[std::string(field.key)] = std::move(entry);
        }
        return out;
    }

    template <typename T>
    nlohmann::json schemaJson(const SectionTable<T>& table, std::string description)
    {
        nlohmann::json properties = nlohmann::json::object();
        for (const FieldDesc<T>& field : table.fields)
        {
            if (!field.persisted || !field.setHint.empty())
            {
                continue;
            }
            std::string help(field.help);
            if (field.weatherDriven)
            {
                help += " Overridden live while weather is enabled.";
            }
            properties[std::string(field.key)] =
                fieldSchema(field.kind, field.min, field.max, field.enumNames, std::move(help));
        }
        nlohmann::json out = schema::object(std::move(properties));
        out["description"] = std::move(description);
        out["additionalProperties"] = false;
        return out;
    }

    template <typename T>
    nlohmann::json diffJson(const SectionTable<T>& table, const T& from, const T& to)
    {
        nlohmann::json out = nlohmann::json::object();
        for (const FieldDesc<T>& field : table.fields)
        {
            if (!field.persisted)
            {
                continue;
            }
            nlohmann::json after = field.get(to);
            if (field.get(from) != after)
            {
                out[std::string(field.key)] = std::move(after);
            }
        }
        return out;
    }

    template <typename T>
    std::vector<std::string> weatherDrivenKeys(const SectionTable<T>& table)
    {
        std::vector<std::string> keys;
        for (const FieldDesc<T>& field : table.fields)
        {
            if (field.weatherDriven)
            {
                keys.emplace_back(field.key);
            }
        }
        return keys;
    }

    template nlohmann::json toJson(const SectionTable<AtmosphereSettings>&, const AtmosphereSettings&);
    template nlohmann::json toJson(const SectionTable<CloudSettings>&, const CloudSettings&);
    template nlohmann::json toJson(const SectionTable<FogSettings>&, const FogSettings&);
    template nlohmann::json toJson(const SectionTable<GISettings>&, const GISettings&);

    template AtmosphereSettings strictPatch(const SectionTable<AtmosphereSettings>&, const AtmosphereSettings&,
                                            const nlohmann::json&, std::string_view);
    template CloudSettings strictPatch(const SectionTable<CloudSettings>&, const CloudSettings&, const nlohmann::json&,
                                       std::string_view);
    template FogSettings strictPatch(const SectionTable<FogSettings>&, const FogSettings&, const nlohmann::json&,
                                     std::string_view);
    template GISettings strictPatch(const SectionTable<GISettings>&, const GISettings&, const nlohmann::json&,
                                    std::string_view);

    template nlohmann::json rangesJson(const SectionTable<AtmosphereSettings>&);
    template nlohmann::json rangesJson(const SectionTable<CloudSettings>&);
    template nlohmann::json rangesJson(const SectionTable<FogSettings>&);
    template nlohmann::json rangesJson(const SectionTable<GISettings>&);

    template nlohmann::json schemaJson(const SectionTable<AtmosphereSettings>&, std::string);
    template nlohmann::json schemaJson(const SectionTable<CloudSettings>&, std::string);
    template nlohmann::json schemaJson(const SectionTable<FogSettings>&, std::string);
    template nlohmann::json schemaJson(const SectionTable<GISettings>&, std::string);

    template nlohmann::json diffJson(const SectionTable<AtmosphereSettings>&, const AtmosphereSettings&,
                                     const AtmosphereSettings&);
    template nlohmann::json diffJson(const SectionTable<CloudSettings>&, const CloudSettings&, const CloudSettings&);
    template nlohmann::json diffJson(const SectionTable<FogSettings>&, const FogSettings&, const FogSettings&);
    template nlohmann::json diffJson(const SectionTable<GISettings>&, const GISettings&, const GISettings&);

    template std::vector<std::string> weatherDrivenKeys(const SectionTable<AtmosphereSettings>&);
    template std::vector<std::string> weatherDrivenKeys(const SectionTable<CloudSettings>&);
    template std::vector<std::string> weatherDrivenKeys(const SectionTable<FogSettings>&);
    template std::vector<std::string> weatherDrivenKeys(const SectionTable<GISettings>&);

    // ------------------------------------------------------------------
    // Values and arguments
    // ------------------------------------------------------------------

    nlohmann::json floatJson(float value)
    {
        if (!std::isfinite(value))
        {
            return nullptr;
        }
        std::array<char, 64> text{};
        const std::to_chars_result written = std::to_chars(text.data(), text.data() + text.size(), value);
        double shortest = static_cast<double>(value);
        if (written.ec == std::errc{})
        {
            double parsed = 0.0;
            const std::from_chars_result read = std::from_chars(text.data(), written.ptr, parsed);
            if (read.ec == std::errc{})
            {
                shortest = parsed;
            }
        }
        return shortest;
    }

    nlohmann::json vec3Json(const glm::vec3& value)
    {
        return nlohmann::json::array({floatJson(value.x), floatJson(value.y), floatJson(value.z)});
    }

    float requireFloat(const nlohmann::json& value, double min, double max, std::string_view path)
    {
        if (!value.is_number())
        {
            throw ArgError("'" + std::string(path) + "' must be a number (got " + valueText(value) + ")");
        }
        const double number = value.get<double>();
        // Validated as a double first: 1e39 is a finite double but would not fit a float.
        if (!std::isfinite(number) || std::fabs(number) > static_cast<double>(std::numeric_limits<float>::max()))
        {
            throw ArgError(rangeError(path, min, max, value));
        }
        const float narrowed = static_cast<float>(number);
        if (narrowed < static_cast<float>(min) || narrowed > static_cast<float>(max))
        {
            throw ArgError(rangeError(path, min, max, value));
        }
        return narrowed;
    }

    glm::vec3 requireVec3(const nlohmann::json& value, double min, double max, std::string_view path)
    {
        if (!value.is_array() || value.size() != 3)
        {
            throw ArgError("'" + std::string(path) + "' must be an array of 3 numbers (got " + valueText(value) + ")");
        }
        glm::vec3 out(0.0f);
        for (std::size_t i = 0; i < 3; ++i)
        {
            out[static_cast<glm::length_t>(i)] =
                requireFloat(value[i], min, max, std::string(path) + "[" + std::to_string(i) + "]");
        }
        return out;
    }

    bool requireBool(const nlohmann::json& value, std::string_view path)
    {
        if (!value.is_boolean())
        {
            throw ArgError("'" + std::string(path) + "' must be a boolean (got " + valueText(value) + ")");
        }
        return value.get<bool>();
    }

    void rejectUnknownArgs(const nlohmann::json& args, std::initializer_list<std::string_view> allowed,
                           std::string_view toolName)
    {
        if (args.is_null())
        {
            return;
        }
        if (!args.is_object())
        {
            throw ArgError("the arguments of " + std::string(toolName) + " must be an object");
        }
        for (auto it = args.begin(); it != args.end(); ++it)
        {
            if (std::find(allowed.begin(), allowed.end(), std::string_view(it.key())) == allowed.end())
            {
                throw ArgError("unknown argument '" + it.key() + "' for " + std::string(toolName) +
                               "; valid arguments: " + joined(allowed));
            }
        }
    }

    void rejectUnknownKeys(const nlohmann::json& value, std::initializer_list<std::string_view> allowed,
                           std::string_view path)
    {
        if (!value.is_object())
        {
            throw ArgError("'" + std::string(path) + "' must be an object (got " + valueText(value) + ")");
        }
        for (auto it = value.begin(); it != value.end(); ++it)
        {
            if (std::find(allowed.begin(), allowed.end(), std::string_view(it.key())) == allowed.end())
            {
                throw ArgError("unknown " + std::string(path) + " key '" + it.key() + "'; valid keys: " + joined(allowed));
            }
        }
    }

    // ------------------------------------------------------------------
    // Sections
    // ------------------------------------------------------------------

    std::string_view sectionName(const SectionValue& value)
    {
        return std::visit([](const auto& section) -> std::string_view
        {
            using S = std::decay_t<decltype(section)>;
            if constexpr (std::is_same_v<S, AtmosphereSettings>)
            {
                return atmosphereTable().name;
            }
            else if constexpr (std::is_same_v<S, CloudSettings>)
            {
                return cloudTable().name;
            }
            else if constexpr (std::is_same_v<S, FogSettings>)
            {
                return fogTable().name;
            }
            else
            {
                static_assert(std::is_same_v<S, GISettings>);
                return giTable().name;
            }
        }, value);
    }

    void writeSection(const SectionValue& value)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // The scene copy: re-read now, so an edit of another section since the caller read it survives.
        types::RenderSettings settings = dispatcher.query(events::scene::GetRenderSettingsQuery{});
        events::scene::RenderSettingsSection section = events::scene::RenderSettingsSection::All;
        std::visit([&settings, &section](const auto& staged)
        {
            using S = std::decay_t<decltype(staged)>;
            if constexpr (std::is_same_v<S, AtmosphereSettings>)
            {
                settings.atmosphere = staged;
                section = events::scene::RenderSettingsSection::Atmosphere;
            }
            else if constexpr (std::is_same_v<S, CloudSettings>)
            {
                settings.cloud = staged;
                section = events::scene::RenderSettingsSection::Cloud;
            }
            else if constexpr (std::is_same_v<S, FogSettings>)
            {
                settings.postProcess.volumetricFog = staged;
                section = events::scene::RenderSettingsSection::VolumetricFog;
            }
            else
            {
                static_assert(std::is_same_v<S, GISettings>);
                // The debug views are session state, not scene data.
                GISettings authored = staged;
                authored.showProbes = false;
                authored.showCascadeBounds = false;
                authored.showProbeValidity = false;
                settings.gi = authored;
                section = events::scene::RenderSettingsSection::GI;
            }
        }, value);

        events::scene::SetRenderSettingsCommand store;
        store.settings = std::move(settings);
        if (!dispatcher.execute(store))
        {
            throw std::runtime_error("Cannot store the render settings: no scene is open (scene_new or scene_load first)");
        }

        // The live copy.
        std::visit([&dispatcher](const auto& staged)
        {
            using S = std::decay_t<decltype(staged)>;
            if constexpr (std::is_same_v<S, AtmosphereSettings>)
            {
                events::atmosphere::ApplyAtmosphereSettingsNoSunCommand apply;
                apply.settings = staged;
                dispatcher.execute(apply);
            }
            else if constexpr (std::is_same_v<S, CloudSettings>)
            {
                events::cloud::ApplyCloudSettingsCommand apply;
                apply.settings = staged;
                dispatcher.execute(apply);
            }
            else if constexpr (std::is_same_v<S, FogSettings>)
            {
                events::postprocess::ApplyPostProcessSettingsCommand apply;
                apply.settings = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                apply.settings.volumetricFog = staged;
                dispatcher.execute(apply);
            }
            else
            {
                static_assert(std::is_same_v<S, GISettings>);
                const GISettings current = dispatcher.query(events::render::gi::GetGISettingsQuery{});
                events::render::gi::ApplyGISettingsCommand apply;
                apply.settings = staged;
                apply.settings.showProbes = current.showProbes;
                apply.settings.showCascadeBounds = current.showCascadeBounds;
                apply.settings.showProbeValidity = current.showProbeValidity;
                dispatcher.execute(apply);
            }
        }, value);

        publishChanged(section);
    }

    // ------------------------------------------------------------------
    // Engine state
    // ------------------------------------------------------------------

    std::optional<std::string> editBlocker(std::string_view action)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        if (dispatcher.query(events::editor::IsPlayModeQuery{}))
        {
            return "Cannot " + std::string(action) + " in Play mode; call play_stop first.";
        }
        bool loading = false;
        try
        {
            loading = dispatcher.query(events::scene::IsSceneLoadPendingQuery{});
        }
        catch (const std::runtime_error&)
        {
            // No scene persistence service: nothing can be loading.
        }
        if (loading)
        {
            return "Cannot " + std::string(action) +
                   " while a scene load is in progress; retry when the load has finished.";
        }
        return std::nullopt;
    }

    bool isWeatherEnabled()
    {
        try
        {
            return events::EventDispatcher::instance().query(events::weather::IsWeatherEnabledQuery{});
        }
        catch (const std::runtime_error&)
        {
            return false;
        }
    }

    std::optional<services::EntityHandle> sunEntity(bool activeOnly)
    {
        events::atmosphere::GetSunEntityQuery query;
        query.activeOnly = activeOnly;
        std::optional<services::EntityHandle> sun = events::EventDispatcher::instance().query(query);
        if (sun.has_value() && !sun->isValid())
        {
            return std::nullopt;
        }
        return sun;
    }

    std::optional<services::IBLData> rootIbl()
    {
        events::scene::GetIBLDataQuery query;
        query.entity = sceneRoot();
        return events::EventDispatcher::instance().query(query);
    }

    void applyIbl(const std::optional<services::IBLData>& data, bool rebake)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        const services::EntityHandle root = sceneRoot();
        if (!data.has_value())
        {
            dispatcher.execute(events::render::RemoveIBLCommand{});
            events::scene::RemoveIBLComponentCommand remove;
            remove.entity = root;
            dispatcher.execute(remove); // false: the root had no IBL component, which is the goal
        }
        else
        {
            events::scene::SetIBLDataCommand store;
            store.entity = root;
            store.iblData = *data;
            if (!dispatcher.execute(store))
            {
                throw std::runtime_error("The scene root rejected the IBL component (is a scene open?)");
            }

            if (!data->hdrRef.isValid())
            {
                // What a scene load does with a component that has no HDR: no environment, neutral knobs.
                dispatcher.execute(events::render::RemoveIBLCommand{});
            }
            else
            {
                if (rebake)
                {
                    const std::string path = data->hdrRef.resolve();
                    if (path.empty())
                    {
                        throw std::runtime_error("The HDR asset guid:" + data->hdrRef.toHexString() +
                                                 " no longer resolves to a file");
                    }
                    events::render::SetIBLCommand bake;
                    bake.hdrPath = path;
                    if (!dispatcher.execute(bake))
                    {
                        throw std::runtime_error("The renderer could not load the HDR environment '" +
                                                 engineFileText(path) + "' (see logs_read)");
                    }
                }
                events::render::SetIBLParamsCommand params;
                params.intensity = data->intensity;
                params.rotationDeg = data->rotationDeg;
                params.tint = data->tint;
                dispatcher.execute(params);
            }
        }
        publishChanged(events::scene::RenderSettingsSection::IBL);
    }

    bool sameIbl(const std::optional<services::IBLData>& a, const std::optional<services::IBLData>& b)
    {
        if (a.has_value() != b.has_value())
        {
            return false;
        }
        if (!a.has_value())
        {
            return true;
        }
        return a->hdrRef == b->hdrRef && a->intensity == b->intensity && a->rotationDeg == b->rotationDeg &&
               a->tint == b->tint;
    }

    WeatherSnapshot readWeather()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        WeatherSnapshot snapshot;
        snapshot.enabled = dispatcher.query(events::weather::IsWeatherEnabledQuery{});
        snapshot.state = dispatcher.query(events::weather::GetWeatherStateQuery{});
        return snapshot;
    }

    void applyWeather(const WeatherSnapshot& snapshot)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::weather::SetWeatherEnabledCommand enable;
        enable.enabled = snapshot.enabled;
        dispatcher.execute(enable);

        // After the switch: turning weather off resets its state, which this then overwrites.
        events::weather::SetWeatherImmediateCommand state;
        state.state = snapshot.state;
        dispatcher.execute(state);

        if (snapshot.enabled)
        {
            events::weather::UpdateWeatherCommand update;
            update.deltaTime = 0.0f;
            dispatcher.execute(update);
        }
    }

    bool sameWeatherState(const ::weather::WeatherState& a, const ::weather::WeatherState& b, float tolerance)
    {
        const auto within = [tolerance](float x, float y) { return std::fabs(x - y) <= tolerance; };
        return within(a.cloudCoverage, b.cloudCoverage) && within(a.cloudDensity, b.cloudDensity) &&
               within(a.cloudType, b.cloudType) && a.precipType == b.precipType &&
               within(a.precipIntensity, b.precipIntensity) && within(a.windSpeed, b.windSpeed) &&
               within(a.windDirectionDeg, b.windDirectionDeg) && within(a.gustStrength, b.gustStrength) &&
               within(a.gustFrequency, b.gustFrequency) && within(a.fogDensity, b.fogDensity) &&
               within(a.heightFogDensity, b.heightFogDensity) && within(a.atmosphereTint.x, b.atmosphereTint.x) &&
               within(a.atmosphereTint.y, b.atmosphereTint.y) && within(a.atmosphereTint.z, b.atmosphereTint.z) &&
               within(a.ambientLightMult, b.ambientLightMult);
    }
}

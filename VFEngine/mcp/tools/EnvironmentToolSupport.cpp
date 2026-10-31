#include "EnvironmentToolSupport.hpp"
#include "PathSandbox.hpp"
#include "RenderSettingsToolSupport.hpp"
#include "ToolHelpers.hpp"

#include "events/EventDispatcher.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/project/ApplicationEvents.hpp"
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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace mcp::tools::env
{
    namespace
    {
        // Passed for the keys weather overrides live while it is enabled.
        constexpr bool weatherDriven = true;

        // ------------------------------------------------------------------
        // Cross-field checks
        // ------------------------------------------------------------------

        std::string checkAtmosphere(const AtmosphereSettings& value)
        {
            if (value.atmosphereRadius > value.planetRadius)
            {
                return {};
            }
            return "atmosphereRadius (" + detail::floatText(value.atmosphereRadius) +
                   ") must be greater than planetRadius (" + detail::floatText(value.planetRadius) + ")";
        }

        std::string checkClouds(const CloudSettings& value)
        {
            if (value.cloudMinAltitude < value.cloudMaxAltitude)
            {
                return {};
            }
            return "cloudMinAltitude (" + detail::floatText(value.cloudMinAltitude) + ") must be below cloudMaxAltitude (" +
                   detail::floatText(value.cloudMaxAltitude) + ")";
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

        // ------------------------------------------------------------------
        // Sections: where each is stored, and the notification that names it
        // ------------------------------------------------------------------

        // Sections pushed live through ApplyPostProcessSettingsCommand.
        template <typename S>
        constexpr bool isPostProcessSection = isPostProcessMember<S> || std::is_same_v<S, PostProcessMasterSettings>;

        template <typename S>
        constexpr events::scene::RenderSettingsSection sectionId()
        {
            using Section = events::scene::RenderSettingsSection;
            if constexpr (std::is_same_v<S, AtmosphereSettings>)
            {
                return Section::Atmosphere;
            }
            else if constexpr (std::is_same_v<S, CloudSettings>)
            {
                return Section::Cloud;
            }
            else if constexpr (std::is_same_v<S, FogSettings>)
            {
                return Section::VolumetricFog;
            }
            else if constexpr (std::is_same_v<S, GISettings>)
            {
                return Section::GI;
            }
            else if constexpr (std::is_same_v<S, ShadowQualitySettings>)
            {
                return Section::Shadows;
            }
            else if constexpr (std::is_same_v<S, DisplayAntiAliasingSettings>)
            {
                return Section::Display;
            }
            else
            {
                static_assert(isPostProcessSection<S>, "every section has a notification");
                return Section::PostProcess;
            }
        }

        // The section into the scene's settings.
        template <typename S>
        void storeSection(types::RenderSettings& settings, const S& staged)
        {
            if constexpr (std::is_same_v<S, AtmosphereSettings>)
            {
                settings.atmosphere = staged;
            }
            else if constexpr (std::is_same_v<S, CloudSettings>)
            {
                settings.cloud = staged;
            }
            else if constexpr (std::is_same_v<S, GISettings>)
            {
                // The debug views are session state, not scene data.
                GISettings authored = staged;
                authored.showProbes = false;
                authored.showCascadeBounds = false;
                authored.showProbeValidity = false;
                settings.gi = authored;
            }
            else if constexpr (std::is_same_v<S, PostProcessMasterSettings>)
            {
                settings.postProcess.enabled = staged.enabled;
            }
            else if constexpr (isPostProcessMember<S>)
            {
                settings.postProcess.*postProcessMember<S>() = staged;
            }
            else if constexpr (std::is_same_v<S, ShadowQualitySettings>)
            {
                storeShadowQuality(settings.shadows, staged);
            }
            else
            {
                static_assert(std::is_same_v<S, DisplayAntiAliasingSettings>, "every section is stored");
                settings.display.msaa = staged.msaa;
            }
        }

        // A post-process section into the live post-process settings.
        template <typename S>
        void storeLivePostProcess(PostProcessSettings& live, const S& staged)
        {
            if constexpr (std::is_same_v<S, PostProcessMasterSettings>)
            {
                live.enabled = staged.enabled;
            }
            else
            {
                live.*postProcessMember<S>() = staged;
            }
        }

        template <typename S>
        const S* findSection(std::span<const SectionValue> values)
        {
            for (const SectionValue& value : values)
            {
                if (const S* section = std::get_if<S>(&value))
                {
                    return section;
                }
            }
            return nullptr;
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
            else if constexpr (std::is_same_v<S, GISettings>)
            {
                return giTable().name;
            }
            else if constexpr (std::is_same_v<S, PostProcessMasterSettings>)
            {
                return postProcessMasterTable().name;
            }
            else if constexpr (std::is_same_v<S, ToneMappingSettings>)
            {
                return toneMappingTable().name;
            }
            else if constexpr (std::is_same_v<S, AutoExposureSettings>)
            {
                return autoExposureTable().name;
            }
            else if constexpr (std::is_same_v<S, BloomSettings>)
            {
                return bloomTable().name;
            }
            else if constexpr (std::is_same_v<S, ColorGradingSettings>)
            {
                return colorGradingTable().name;
            }
            else if constexpr (std::is_same_v<S, SSAOSettings>)
            {
                return ssaoTable().name;
            }
            else if constexpr (std::is_same_v<S, VignetteSettings>)
            {
                return vignetteTable().name;
            }
            else if constexpr (std::is_same_v<S, ChromaticAberrationSettings>)
            {
                return chromaticAberrationTable().name;
            }
            else if constexpr (std::is_same_v<S, FilmGrainSettings>)
            {
                return filmGrainTable().name;
            }
            else if constexpr (std::is_same_v<S, DepthOfFieldSettings>)
            {
                return depthOfFieldTable().name;
            }
            else if constexpr (std::is_same_v<S, EdgeDetectionSettings>)
            {
                return edgeDetectionTable().name;
            }
            else if constexpr (std::is_same_v<S, FXAASettings>)
            {
                return fxaaTable().name;
            }
            else if constexpr (std::is_same_v<S, UpscaleSettings>)
            {
                return upscaleTable().name;
            }
            else if constexpr (std::is_same_v<S, ShadowQualitySettings>)
            {
                return shadowTable().name;
            }
            else
            {
                static_assert(std::is_same_v<S, DisplayAntiAliasingSettings>);
                return displayTable().name;
            }
        }, value);
    }

    void writeSections(std::span<const SectionValue> values)
    {
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            for (std::size_t j = i + 1; j < values.size(); ++j)
            {
                if (values[i].index() == values[j].index())
                {
                    throw std::logic_error("writeSections: the " + std::string(sectionName(values[i])) +
                                           " section is listed twice");
                }
            }
        }
        if (values.empty())
        {
            return;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        // The scene copy: re-read now, so an edit of another section since the caller read it survives.
        events::scene::SetRenderSettingsCommand store;
        store.settings = dispatcher.query(events::scene::GetRenderSettingsQuery{});
        for (const SectionValue& value : values)
        {
            std::visit([&store](const auto& staged) { storeSection(store.settings, staged); }, value);
        }
        if (!dispatcher.execute(store))
        {
            throw std::runtime_error("Cannot store the render settings: no scene is open (scene_new or scene_load first)");
        }

        // The live copies, each pushed at most once.
        if (const AtmosphereSettings* atmosphere = findSection<AtmosphereSettings>(values))
        {
            events::atmosphere::ApplyAtmosphereSettingsNoSunCommand apply;
            apply.settings = *atmosphere;
            dispatcher.execute(apply);
        }
        if (const CloudSettings* clouds = findSection<CloudSettings>(values))
        {
            events::cloud::ApplyCloudSettingsCommand apply;
            apply.settings = *clouds;
            dispatcher.execute(apply);
        }
        const bool postProcess = std::any_of(values.begin(), values.end(), [](const SectionValue& value)
        {
            return std::visit([](const auto& section) { return isPostProcessSection<std::decay_t<decltype(section)>>; },
                              value);
        });
        if (postProcess)
        {
            // Only the staged sections replace the live ones: weather's live fog, the runtime-driven effects
            // (underwater, rain droplets) and everything the tools do not own keep their live values.
            events::postprocess::ApplyPostProcessSettingsCommand apply;
            apply.settings = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
            for (const SectionValue& value : values)
            {
                std::visit([&apply](const auto& staged)
                {
                    using S = std::decay_t<decltype(staged)>;
                    if constexpr (isPostProcessSection<S>)
                    {
                        storeLivePostProcess(apply.settings, staged);
                    }
                }, value);
            }
            dispatcher.execute(apply);
        }
        if (const GISettings* gi = findSection<GISettings>(values))
        {
            const GISettings current = dispatcher.query(events::render::gi::GetGISettingsQuery{});
            events::render::gi::ApplyGISettingsCommand apply;
            apply.settings = *gi;
            apply.settings.showProbes = current.showProbes;
            apply.settings.showCascadeBounds = current.showCascadeBounds;
            apply.settings.showProbeValidity = current.showProbeValidity;
            dispatcher.execute(apply);
        }
        if (findSection<ShadowQualitySettings>(values) != nullptr)
        {
            // The whole stored bundle, like the Render window's Apply (shadows, culling, terrain, ...).
            events::render::ApplyShadowSettingsCommand apply;
            apply.settings = store.settings;
            dispatcher.execute(apply);
        }
        if (findSection<DisplayAntiAliasingSettings>(values) != nullptr)
        {
            events::application::ApplyDisplaySettingsNotification display;
            display.presentMode = store.settings.display.presentMode;
            display.msaa = store.settings.display.msaa;
            dispatcher.publish(display);
        }

        // One notification per distinct section, in the order given.
        std::vector<events::scene::RenderSettingsSection> published;
        for (const SectionValue& value : values)
        {
            const events::scene::RenderSettingsSection section =
                std::visit([](const auto& staged) { return sectionId<std::decay_t<decltype(staged)>>(); }, value);
            if (std::find(published.begin(), published.end(), section) == published.end())
            {
                published.push_back(section);
                publishChanged(section);
            }
        }
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

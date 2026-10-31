#include "RenderSettingsToolSupport.hpp"
#include "PathSandbox.hpp"
#include "TerrainToolSupport.hpp"

#include "events/EventDispatcher.hpp"

#include <exception>
#include <filesystem>
#include <stdexcept>
#include <system_error>

namespace mcp::tools::env
{
    namespace
    {
        namespace fs = std::filesystem;

        // ToneMappingEffect::getPriority() (graphics): plugin effects below it run on linear HDR, above it on
        // the tone-mapped image.
        constexpr uint32_t tonemapPriority = 100;

        // The editor's SSAO quality combo also sets the sample count the effect actually uses
        // (PostProcessConfigWindowSections.cpp:149-153; SSAOEffect reads only kernelSize). A patch that
        // changes the quality without naming a kernelSize - or naming the current one, as an echoed
        // render_settings_get object does - does the same; a different kernelSize wins.
        void deriveSsao(const SSAOSettings& base, SSAOSettings& staged, const nlohmann::json& patch)
        {
            // `staged` already holds the patch, so its kernelSize is the patch's when the patch names one.
            if (staged.quality != base.quality && (!patch.contains("kernelSize") || staged.kernelSize == base.kernelSize))
            {
                staged.kernelSize = ::postprocess::ssaoSamplesFromQuality(staged.quality);
            }
        }

        const std::string& storedLutPath(const ColorGradingSettings& value, std::string_view key)
        {
            return key == "primaryLutPath" ? value.primaryLutPath : value.secondaryLutPath;
        }
    }

    // ------------------------------------------------------------------
    // Tables. Ranges: the scene loader's clamp (SceneSerializePostProcess.cpp), or for the shadows (the
    // loader clamps none) the Render window's sliders (RenderConfigWindowSections.cpp), so every accepted
    // value survives a save + load unchanged.
    // ------------------------------------------------------------------

    const SectionTable<PostProcessMasterSettings>& postProcessMasterTable()
    {
        using S = PostProcessMasterSettings;
        static const SectionTable<S> table{
            "post-process",
            {
                boolField<S, &S::enabled>("enabled",
                    "Master switch of the built-in effects: false = none of them renders, tone mapping included "
                    "(plugin effects and the upscaler still run)."),
            },
            nullptr
        };
        return table;
    }

    const SectionTable<ToneMappingSettings>& toneMappingTable()
    {
        using S = ToneMappingSettings;
        static const SectionTable<S> table{
            "tone mapping",
            {
                boolField<S, &S::enabled>("enabled",
                    "Map the HDR scene to the display; exposure, contrast, gamma and mode only show while true."),
                enumField<S, ::postprocess::ToneMappingMode, &S::mode>("mode",
                    "Tone curve: ACES = filmic contrast, AgX = filmic with gentler highlights, KhronosPBRNeutral = "
                    "keeps material colours; Reinhard, Uncharted2, GranTurismo and Linear are classic curves."),
                floatField<S, &S::exposure>("exposure", 0.01, 20.0,
                    "Scene brightness multiplier before the curve (1 = neutral). Ignored while autoExposure.enabled."),
                floatField<S, &S::gamma>("gamma", 0.1, 5.0, "Display gamma (2.2 = standard monitors)."),
                floatField<S, &S::contrast>("contrast", 0.5, 2.0, "Contrast around mid-grey (1 = neutral)."),
                floatField<S, &S::toe>("toe", 0.0, 1.0, "Shadow curve: higher = darker, punchier shadows (0 = neutral)."),
                floatField<S, &S::shoulder>("shoulder", 0.0, 1.0,
                    "Highlight curve: higher = softer highlight roll-off (0 = neutral)."),
            },
            nullptr
        };
        return table;
    }

    const SectionTable<AutoExposureSettings>& autoExposureTable()
    {
        using S = AutoExposureSettings;
        static const SectionTable<S> table{
            "auto exposure",
            {
                boolField<S, &S::enabled>("enabled",
                    "Adapt the exposure to the scene's brightness like an eye; replaces toneMapping.exposure."),
                floatField<S, &S::minExposure>("minExposure", 0.01, 1.0,
                    "Lowest exposure it may pick: keeps bright scenes from going too dark."),
                floatField<S, &S::maxExposure>("maxExposure", 1.0, 20.0,
                    "Highest exposure it may pick: keeps dark scenes from going too bright."),
                floatField<S, &S::adaptSpeedUp>("adaptSpeedUp", 0.1, 10.0,
                    "Adaptation speed when the scene gets brighter (EV per second)."),
                floatField<S, &S::adaptSpeedDown>("adaptSpeedDown", 0.1, 10.0,
                    "Adaptation speed when the scene gets darker (EV per second)."),
                floatField<S, &S::exposureCompensation>("exposureCompensation", -5.0, 5.0,
                    "Artistic offset in EV stops: positive = brighter, negative = darker."),
                floatField<S, &S::lowPercentile>("lowPercentile", 0.0, 0.5,
                    "Fraction of the darkest pixels left out of the measurement."),
                floatField<S, &S::highPercentile>("highPercentile", 0.5, 1.0,
                    "Pixels above this fraction (bright highlights, the sun) are left out of the measurement."),
            },
            nullptr
        };
        return table;
    }

    const SectionTable<BloomSettings>& bloomTable()
    {
        using S = BloomSettings;
        static const SectionTable<S> table{
            "bloom",
            {
                boolField<S, &S::enabled>("enabled", "Glow around bright areas."),
                floatField<S, &S::threshold>("threshold", 0.0, 10.0,
                    "Brightness above which pixels glow; bloom runs after tone mapping, so 0.5-0.8 suits most "
                    "scenes (lower = more bloom)."),
                floatField<S, &S::intensity>("intensity", 0.0, 5.0, "Glow strength."),
                floatField<S, &S::radius>("radius", 0.0, 1.0, "How far the glow spreads."),
                uintField<S, &S::passes>("passes", 1.0, 10.0, "Blur passes: more = smoother, wider glow."),
            },
            nullptr
        };
        return table;
    }

    const SectionTable<ColorGradingSettings>& colorGradingTable()
    {
        using S = ColorGradingSettings;
        static const SectionTable<S> table{
            "color grading",
            {
                boolField<S, &S::enabled>("enabled", "Grade the tone-mapped image: LUTs, then lift / gamma / gain."),
                stringField<S, &S::primaryLutPath>("primaryLutPath", ".cube",
                    "3D colour LUT (.cube file, project-relative); \"\" = none."),
                stringField<S, &S::secondaryLutPath>("secondaryLutPath", ".cube",
                    "Second 3D LUT (.cube) mixed in by lutBlendFactor; \"\" = none."),
                floatField<S, &S::lutIntensity>("lutIntensity", 0.0, 1.0, "LUT strength: 0 = off, 1 = full."),
                floatField<S, &S::lutBlendFactor>("lutBlendFactor", 0.0, 1.0,
                    "0 = the primary LUT only, 1 = the secondary only."),
                floatField<S, &S::liftR>("liftR", -1.0, 1.0, "Lift (shadows), red offset (0 = neutral)."),
                floatField<S, &S::liftG>("liftG", -1.0, 1.0, "Lift (shadows), green offset (0 = neutral)."),
                floatField<S, &S::liftB>("liftB", -1.0, 1.0, "Lift (shadows), blue offset (0 = neutral)."),
                floatField<S, &S::gammaR>("gammaR", 0.01, 5.0, "Gamma (midtones), red (1 = neutral)."),
                floatField<S, &S::gammaG>("gammaG", 0.01, 5.0, "Gamma (midtones), green (1 = neutral)."),
                floatField<S, &S::gammaB>("gammaB", 0.01, 5.0, "Gamma (midtones), blue (1 = neutral)."),
                floatField<S, &S::gainR>("gainR", 0.0, 5.0, "Gain (highlights), red multiplier (1 = neutral)."),
                floatField<S, &S::gainG>("gainG", 0.0, 5.0, "Gain (highlights), green multiplier (1 = neutral)."),
                floatField<S, &S::gainB>("gainB", 0.0, 5.0, "Gain (highlights), blue multiplier (1 = neutral)."),
                floatField<S, &S::saturation>("saturation", 0.0, 3.0, "Saturation (1 = neutral, 0 = greyscale)."),
                floatField<S, &S::colorTemperature>("colorTemperature", 1000.0, 15000.0,
                    "White balance in kelvin: 6500 = neutral daylight, lower = warmer, higher = cooler."),
                floatField<S, &S::colorTint>("colorTint", -1.0, 1.0, "Green-magenta tint (0 = neutral)."),
            },
            nullptr
        };
        return table;
    }

    const SectionTable<SSAOSettings>& ssaoTable()
    {
        using S = SSAOSettings;
        static const SectionTable<S> table{
            "SSAO",
            {
                boolField<S, &S::enabled>("enabled", "Screen-space ambient occlusion: darkens creases and contact areas."),
                enumField<S, ::postprocess::SSAOQuality, &S::quality>("quality",
                    "Sample preset (Low 16, Medium 32, High 48, Ultra 64); changing it also sets kernelSize unless "
                    "a different kernelSize is given."),
                floatField<S, &S::radius>("radius", 0.1, 5.0, "Occlusion radius in world units."),
                floatField<S, &S::bias>("bias", 0.001, 0.1, "Depth bias against self-occlusion artefacts."),
                floatField<S, &S::intensity>("intensity", 0.1, 5.0, "Occlusion strength."),
                intField<S, &S::kernelSize>("kernelSize", 8.0, 64.0,
                    "Samples per pixel - what actually renders (the quality presets set it)."),
                floatField<S, &S::power>("power", 0.5, 5.0, "Occlusion contrast: higher = darker creases."),
            },
            nullptr,
            &deriveSsao
        };
        return table;
    }

    const SectionTable<VignetteSettings>& vignetteTable()
    {
        using S = VignetteSettings;
        static const SectionTable<S> table{
            "vignette",
            {
                boolField<S, &S::enabled>("enabled", "Darken the screen edges."),
                floatField<S, &S::intensity>("intensity", 0.0, 1.0, "Darkening strength."),
                floatField<S, &S::radius>("radius", 0.0, 2.0, "Where the darkening starts, from the centre."),
                floatField<S, &S::softness>("softness", 0.0, 1.0, "Smoothness of the falloff."),
            },
            nullptr
        };
        return table;
    }

    const SectionTable<ChromaticAberrationSettings>& chromaticAberrationTable()
    {
        using S = ChromaticAberrationSettings;
        static const SectionTable<S> table{
            "chromatic aberration",
            {
                boolField<S, &S::enabled>("enabled", "Lens colour fringing toward the screen edges."),
                floatField<S, &S::intensity>("intensity", 0.0, 0.1, "Colour channel separation (0.005 = subtle)."),
            },
            nullptr
        };
        return table;
    }

    const SectionTable<FilmGrainSettings>& filmGrainTable()
    {
        using S = FilmGrainSettings;
        static const SectionTable<S> table{
            "film grain",
            {
                boolField<S, &S::enabled>("enabled", "Animated film grain."),
                floatField<S, &S::intensity>("intensity", 0.0, 1.0, "Grain strength."),
                floatField<S, &S::size>("size", 0.1, 5.0, "Grain size."),
            },
            nullptr
        };
        return table;
    }

    const SectionTable<DepthOfFieldSettings>& depthOfFieldTable()
    {
        using S = DepthOfFieldSettings;
        static const SectionTable<S> table{
            "depth of field",
            {
                boolField<S, &S::enabled>("enabled", "Blur what is out of focus."),
                enumField<S, ::postprocess::DoFFocusMode, &S::focusMode>("focusMode",
                    "Manual = focus at focalDistance; TargetPoint = focus on the world point focusTargetX/Y/Z."),
                floatField<S, &S::focalDistance>("focalDistance", 0.1, 1000.0,
                    "Distance from the camera to the plane in focus, metres (Manual)."),
                // The loader does not clamp the target; this bound only keeps it finite.
                floatField<S, &S::focusTargetX>("focusTargetX", -1.0e6, 1.0e6, "World X of the focus point (TargetPoint)."),
                floatField<S, &S::focusTargetY>("focusTargetY", -1.0e6, 1.0e6, "World Y of the focus point (TargetPoint)."),
                floatField<S, &S::focusTargetZ>("focusTargetZ", -1.0e6, 1.0e6, "World Z of the focus point (TargetPoint)."),
                floatField<S, &S::focusSmoothing>("focusSmoothing", 0.1, 50.0, "How fast the focus follows changes."),
                floatField<S, &S::focalRange>("focalRange", 0.1, 100.0,
                    "Depth around the focal plane that stays sharp, metres."),
                floatField<S, &S::maxBlurRadius>("maxBlurRadius", 0.0, 20.0,
                    "Largest blur, in pixels at display resolution."),
                intField<S, &S::sampleCount>("sampleCount", 4.0, 32.0,
                    "Blur samples (quality vs cost); the shader uses at most 16 (dof.glsl's Poisson disc)."),
            },
            nullptr
        };
        return table;
    }

    const SectionTable<EdgeDetectionSettings>& edgeDetectionTable()
    {
        using S = EdgeDetectionSettings;
        static const SectionTable<S> table{
            "edge detection",
            {
                boolField<S, &S::enabled>("enabled", "Outline the edges of the geometry (toon / stylised look)."),
                floatField<S, &S::threshold>("threshold", 0.01, 1.0, "Edge sensitivity: lower = more edges."),
                floatField<S, &S::edgeWidth>("edgeWidth", 0.5, 3.0, "Outline width in pixels."),
                rgbArrayField<S, &S::edgeColor>("edgeColor", 0.0, 1.0, "RGB outline colour."),
                floatField<S, &S::opacity>("opacity", 0.0, 1.0, "Outline opacity."),
            },
            nullptr
        };
        return table;
    }

    const SectionTable<FXAASettings>& fxaaTable()
    {
        using S = FXAASettings;
        static const SectionTable<S> table{
            "FXAA",
            {
                boolField<S, &S::enabled>("enabled",
                    "FXAA 3.11 anti-aliasing over the tone-mapped image: smooths geometry, alpha-tested and shading "
                    "edges on any GPU, can be combined with display.msaa and still runs after DLSS / DLAA. With "
                    "tone mapping off, edges against highlights brighter than white stay hard."),
                enumField<S, ::postprocess::FXAAQuality, &S::quality>("quality",
                    "Edge-search preset: Low = FXAA preset 10 (fastest), Medium = 12 (FXAA's default), High = 29, "
                    "Ultra = 39 (longest search)."),
                floatField<S, &S::subpixel>("subpixel", 0.0, 1.0,
                    "Sub-pixel aliasing removal: 0 = off (sharpest), 0.75 = default, 1 = softest."),
                floatField<S, &S::edgeThreshold>("edgeThreshold", 0.063, 0.333,
                    "Minimum local contrast to anti-alias, relative to the brightest neighbour: lower = more edges "
                    "and slower (0.125 = high quality, 0.166 = FXAA's default)."),
                floatField<S, &S::edgeThresholdMin>("edgeThresholdMin", 0.0312, 0.0833,
                    "Contrast below which dark areas are skipped: 0.0312 = processes the most, 0.0833 = fastest."),
            },
            nullptr
        };
        return table;
    }

    const SectionTable<UpscaleSettings>& upscaleTable()
    {
        using S = UpscaleSettings;
        static const SectionTable<S> table{
            "upscaler",
            {
                boolField<S, &S::enabled>("enabled",
                    "Run NVIDIA DLSS: temporal anti-aliasing and, below quality Native, rendering at a lower "
                    "resolution. Needs an RTX GPU; elsewhere it stays off."),
                enumField<S, ::postprocess::UpscaleMode, &S::mode>("mode", "Off, DLSS, or Auto (= DLSS when supported)."),
                enumField<S, ::postprocess::UpscaleQuality, &S::quality>("quality",
                    "Native = DLAA (anti-aliasing only, full resolution); Quality, Balanced, Performance and "
                    "UltraPerformance render 1.5x, 1.7x, 2x and 3x fewer pixels per axis."),
                boolField<S, &S::rayReconstruction>("rayReconstruction",
                    "DLSS Ray Reconstruction (denoises ray-traced effects) where the GPU supports it."),
                boolField<S, &S::forceTraditionalDenoiser>("forceTraditionalDenoiser",
                    "Keep the engine's ray-traced shadow denoiser running alongside Ray Reconstruction (A/B tests)."),
            },
            nullptr
        };
        return table;
    }

    const SectionTable<ShadowQualitySettings>& shadowTable()
    {
        using S = ShadowQualitySettings;
        static const SectionTable<S> table{
            "shadows",
            {
                boolField<S, &S::enabled>("enabled", "Render shadows."),
                enumField<S, types::ShadowQuality, &S::quality>("quality",
                    "Shadow map resolution: Low 512, Medium 1024, High 2048, Ultra 4096; Off = no shadows "
                    "(like enabled:false)."),
                floatField<S, &S::shadowBias>("shadowBias", 0.0, 0.1, "Constant depth offset against shadow acne."),
                floatField<S, &S::slopeBias>("slopeBias", 0.0, 5.0,
                    "Slope-scaled bias for surfaces at grazing angles."),
                floatField<S, &S::normalBias>("normalBias", 0.0, 1.0,
                    "Offset along the surface normal: higher = less acne, but shadows detach from their casters."),
                boolField<S, &S::softShadows>("softShadows",
                    "Soft, contact-hardening shadows (PCSS); false = hard shadows."),
                floatField<S, &S::shadowIntensity>("shadowIntensity", 0.0, 1.0,
                    "Shadow darkness: 0 = lighter (ambient light in the shadow), 1 = darkest."),
            },
            nullptr
        };
        return table;
    }

    const SectionTable<DisplayAntiAliasingSettings>& displayTable()
    {
        using S = DisplayAntiAliasingSettings;
        static const SectionTable<S> table{
            "anti-aliasing",
            {
                enumField<S, types::MsaaSamples, &S::msaa>("msaa",
                    "Multisample anti-aliasing of the scene geometry, clamped to what the GPU supports. Ignored "
                    "while the upscaler is active (DLSS / DLAA anti-alias the image themselves)."),
            },
            nullptr
        };
        return table;
    }

    std::optional<std::string_view> unsettablePostProcessKey(std::string_view key)
    {
        if (key == "volumetricFog")
        {
            return "set the volumetric fog with fog_set";
        }
        if (key == "underwater")
        {
            return "the underwater effect follows the water while the camera is submerged";
        }
        if (key == "rainDroplets")
        {
            return "rain droplets follow the weather (weather_set)";
        }
        if (key == "ssr")
        {
            return "screen-space reflections are not saved with the scene";
        }
        if (key == "frameGen" || key == "reflex")
        {
            return "frame generation and Reflex are latency features, not part of the look; set them in the "
                   "editor's Post Process window";
        }
        return std::nullopt;
    }

    // ------------------------------------------------------------------
    // Colour-grading LUT paths
    // ------------------------------------------------------------------

    nlohmann::json resolveLutPaths(const nlohmann::json& patch, const ColorGradingSettings& current,
                                   std::string_view prefix)
    {
        if (!patch.is_object())
        {
            return patch; // strictPatch reports it
        }

        nlohmann::json out = patch;
        std::optional<std::optional<fs::path>> knownRoot;
        const auto reportRoot = [&knownRoot]() -> const std::optional<fs::path>&
        {
            if (!knownRoot.has_value())
            {
                knownRoot = tryTerrainProjectRoot();
            }
            return *knownRoot;
        };

        for (std::string_view key : lutPathKeys)
        {
            const auto it = out.find(std::string(key));
            if (it == out.end() || !it->is_string())
            {
                continue;
            }
            const std::string requested = it->get<std::string>();
            const std::string& stored = storedLutPath(current, key);
            if (requested.empty())
            {
                continue;
            }
            if (requested == stored || requested == reportLutPath(reportRoot(), stored))
            {
                *it = stored;
                continue;
            }

            const std::string path = detail::pathOf(prefix, key);
            std::string error;
            const std::optional<fs::path> resolved = resolveInside(terrainProjectRoot(), requested, error, ".cube");
            if (!resolved.has_value())
            {
                throw ArgError("'" + path + "': " + error);
            }
            std::error_code ec;
            if (!fs::is_regular_file(*resolved, ec))
            {
                throw ArgError("'" + path + "': there is no .cube file at '" + requested +
                               "' (paths are relative to the project folder)");
            }
            try
            {
                *it = resolved->string(); // narrow, like every engine path (ColorGradingEffect opens it as is)
            }
            catch (const std::exception&)
            {
                throw ArgError("'" + path + "': the path '" + requested +
                               "' cannot be stored in the system code page; rename the file");
            }
        }
        return out;
    }

    std::string reportLutPath(const std::optional<fs::path>& root, const std::string& stored)
    {
        if (stored.empty())
        {
            return {};
        }
        return projectRelativeUtf8(root, fs::path(stored));
    }

    void reportLutPaths(nlohmann::json& colorGrading, const std::optional<fs::path>& root)
    {
        for (std::string_view key : lutPathKeys)
        {
            const auto it = colorGrading.find(std::string(key));
            if (it != colorGrading.end() && it->is_string())
            {
                *it = reportLutPath(root, it->get<std::string>());
            }
        }
    }

    std::vector<std::string> missingLutPaths(const ColorGradingSettings& value)
    {
        std::vector<std::string> missing;
        for (std::string_view key : lutPathKeys)
        {
            const std::string& stored = storedLutPath(value, key);
            std::error_code ec;
            if (!stored.empty() && !fs::is_regular_file(fs::path(stored), ec))
            {
                // Stored in the narrow (ANSI) form the effect opens; reported as UTF-8 like every tool path.
                missing.push_back(pathToUtf8(fs::path(stored)));
            }
        }
        return missing;
    }

    // ------------------------------------------------------------------
    // Plugin post-process effects
    // ------------------------------------------------------------------

    std::vector<events::editor::PluginPostProcessEffectInfo> listPluginEffects()
    {
        return events::EventDispatcher::instance().query(events::editor::ListPluginPostProcessEffectsQuery{});
    }

    bool setPluginEffectEnabled(uint64_t id, bool enabled)
    {
        events::editor::SetPluginPostProcessEffectEnabledCommand command;
        command.id = id;
        command.enabled = enabled;
        return events::EventDispatcher::instance().execute(command);
    }

    nlohmann::json pluginEffectJson(const events::editor::PluginPostProcessEffectInfo& effect)
    {
        return {
            {"id", effect.id},
            {"plugin", effect.plugin},
            {"name", effect.name},
            {"priority", effect.priority},
            {"stage", effect.priority < tonemapPriority ? "beforeTonemap" : "afterTonemap"},
            {"enabled", effect.requestedEnabled},
            {"pluginActive", effect.pluginActive},
            {"rendering", effect.requestedEnabled && effect.pluginActive}
        };
    }
}

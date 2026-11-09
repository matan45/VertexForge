#include "CoreTools.hpp"
#include "RenderSettingsToolSupport.hpp"
#include "TerrainToolSupport.hpp"
#include "ToolHelpers.hpp"
#include "../protocol/ArgReader.hpp"
#include "../undo/EnvironmentUndo.hpp"
#include "../undo/McpUndo.hpp"
#include "../undo/PluginEffectUndo.hpp"

#include "events/EventDispatcher.hpp"
#include "events/render/PostProcessEvents.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "types/ShadowPolicy.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// VK-1655 post-processing / render settings tools: read and tune the scene's look (tone mapping,
// exposure, bloom, colour grading, SSAO and the other saved post-process effects, the upscaler, MSAA
// and the shadow quality), plus the plugin post-process effects (VK-1409). Like the VK-1654 environment
// tools, every edit refuses in Play mode and while a scene load is pending, validates the WHOLE request
// before touching the engine, writes all its sections through ONE env::writeSections (scene copy + live
// copy + notifications) and pushes exactly one undo batch - none when nothing changes.
namespace mcp::tools
{
    namespace
    {
        namespace fs = std::filesystem;
        using events::editor::PluginPostProcessEffectInfo;

        // An upscaler change waits for the GPU and rebuilds the render targets (OffScreenControllerSettings.cpp).
        constexpr std::chrono::milliseconds renderSettingsTimeout{30000};

        // ------------------------------------------------------------------
        // Reading
        // ------------------------------------------------------------------

        types::RenderSettings authoredSettings()
        {
            return events::EventDispatcher::instance().query(events::scene::GetRenderSettingsQuery{});
        }

        env::PostProcessSettings livePostProcess()
        {
            return events::EventDispatcher::instance().query(events::postprocess::GetPostProcessSettingsQuery{});
        }

        // nullopt when no upscale-status handler is registered.
        std::optional<services::UpscaleStatus> upscaleStatus()
        {
            try
            {
                return events::EventDispatcher::instance().query(events::postprocess::GetUpscaleStatusQuery{});
            }
            catch (const std::runtime_error&)
            {
                return std::nullopt;
            }
        }

        env::PostProcessMasterSettings masterOf(const env::PostProcessSettings& postProcess)
        {
            return env::PostProcessMasterSettings{postProcess.enabled};
        }

        env::DisplayAntiAliasingSettings displayOf(const types::RenderSettings& settings)
        {
            return env::DisplayAntiAliasingSettings{settings.display.msaa};
        }

        template <typename E, std::size_t N>
        std::string enumText(const std::array<std::string_view, N>& names, E value)
        {
            const auto index = static_cast<std::size_t>(value);
            return index < N ? std::string(names[index]) : std::to_string(index);
        }

        // ------------------------------------------------------------------
        // The settings as JSON, in the shape render_settings_set takes
        // ------------------------------------------------------------------

        nlohmann::json postProcessJson(const env::PostProcessSettings& postProcess, const std::optional<fs::path>& root)
        {
            nlohmann::json out = env::toJson(env::postProcessMasterTable(), masterOf(postProcess));
            env::forEachPostProcessSection([&](std::string_view key, const auto& table, auto member)
            {
                nlohmann::json section = env::toJson(table, postProcess.*member);
                if (key == "colorGrading")
                {
                    env::reportLutPaths(section, root);
                }
                out[std::string(key)] = std::move(section);
            });
            return out;
        }

        nlohmann::json settingsJson(const types::RenderSettings& settings, const std::optional<fs::path>& root)
        {
            return {
                {"postProcess", postProcessJson(settings.postProcess, root)},
                {"shadows", env::toJson(env::shadowTable(), env::shadowQualityOf(settings.shadows))},
                {"display", env::toJson(env::displayTable(), displayOf(settings))}
            };
        }

        nlohmann::json rangesJson()
        {
            nlohmann::json postProcess = env::rangesJson(env::postProcessMasterTable());
            env::forEachPostProcessSection([&postProcess](std::string_view key, const auto& table, auto)
            {
                postProcess[std::string(key)] = env::rangesJson(table);
            });
            return {
                {"postProcess", std::move(postProcess)},
                {"shadows", env::rangesJson(env::shadowTable())},
                {"display", env::rangesJson(env::displayTable())}
            };
        }

        // The post-process keys whose LIVE value differs from the saved one (an unsaved editor change, a
        // script in an earlier Play session, ...), with the live value.
        nlohmann::json liveDiffJson(const env::PostProcessSettings& authored, const env::PostProcessSettings& live,
                                    const std::optional<fs::path>& root)
        {
            nlohmann::json postProcess = env::diffJson(env::postProcessMasterTable(), masterOf(authored), masterOf(live));
            env::forEachPostProcessSection([&](std::string_view key, const auto& table, auto member)
            {
                nlohmann::json diff = env::diffJson(table, authored.*member, live.*member);
                if (diff.empty())
                {
                    return;
                }
                if (key == "colorGrading")
                {
                    env::reportLutPaths(diff, root);
                }
                postProcess[std::string(key)] = std::move(diff);
            });
            nlohmann::json out = nlohmann::json::object();
            if (!postProcess.empty())
            {
                out["postProcess"] = std::move(postProcess);
            }
            return out;
        }

        // ------------------------------------------------------------------
        // What renders
        // ------------------------------------------------------------------

        bool upscalerActive(const env::PostProcessSettings& live, const std::optional<services::UpscaleStatus>& status)
        {
            if (status.has_value())
            {
                return status->activeMode != ::postprocess::UpscaleMode::Off;
            }
            return live.upscale.enabled && live.upscale.mode != ::postprocess::UpscaleMode::Off;
        }

        nlohmann::json upscalerJson(const env::PostProcessSettings& live, const std::optional<services::UpscaleStatus>& status)
        {
            nlohmann::json out{
                {"requested", live.upscale.enabled && live.upscale.mode != ::postprocess::UpscaleMode::Off},
                {"quality", enumText(env::upscaleQualityNames, live.upscale.quality)}
            };
            if (!status.has_value())
            {
                out["status"] = nullptr; // no upscale-status handler
                return out;
            }
            out["active"] = status->activeMode != ::postprocess::UpscaleMode::Off;
            out["dlssSupported"] = status->dlssSupported;
            out["rayReconstructionActive"] = status->dlssRRActive;
            if (status->renderWidth > 0 && status->displayWidth > 0)
            {
                out["renderResolution"] = {status->renderWidth, status->renderHeight};
                out["displayResolution"] = {status->displayWidth, status->displayHeight};
            }
            return out;
        }

        std::string antiAliasingText(const types::RenderSettings& settings, const env::PostProcessSettings& live,
                                     const std::optional<services::UpscaleStatus>& status)
        {
            std::string text;
            if (upscalerActive(live, status))
            {
                text = live.upscale.quality == ::postprocess::UpscaleQuality::Native ? "DLAA" : "DLSS";
            }
            else if (settings.display.msaa != types::MsaaSamples::Off)
            {
                text = "MSAA " + enumText(env::msaaNames, settings.display.msaa);
            }
            // FXAA is a built-in effect (it needs the master switch) and still runs after the upscaler.
            if (live.enabled && live.fxaa.enabled)
            {
                return text.empty() ? std::string("FXAA") : text + " + FXAA";
            }
            return text.empty() ? std::string("none") : text;
        }

        nlohmann::json exposureJson(const env::PostProcessSettings& live)
        {
            if (!live.enabled || !live.toneMapping.enabled)
            {
                return {{"source", "none"}, {"note", "tone mapping is off, so no exposure is applied"}};
            }
            if (live.autoExposure.enabled)
            {
                return {{"source", "auto"}, {"exposureCompensation", env::floatJson(live.autoExposure.exposureCompensation)}};
            }
            return {{"source", "manual"}, {"exposure", env::floatJson(live.toneMapping.exposure)}};
        }

        nlohmann::json effectiveJson(const types::RenderSettings& settings, const env::PostProcessSettings& live,
                                     const std::optional<services::UpscaleStatus>& status)
        {
            nlohmann::json rendered = nlohmann::json::object();
            env::forEachPostProcessSection([&](std::string_view key, const auto&, auto member)
            {
                if (key != "upscale") // the upscaler is not an effect: it runs even with the master switch off
                {
                    rendered[std::string(key)] = live.enabled && (live.*member).enabled;
                }
            });
            return {
                {"postProcessEnabled", live.enabled},
                {"rendered", std::move(rendered)},
                {"exposure", exposureJson(live)},
                {"upscaler", upscalerJson(live, status)},
                {"antiAliasing", antiAliasingText(settings, live, status)},
                {"shadowsRendered", types::shadowsRendered(settings.shadows)}
            };
        }

        nlohmann::json notesJson()
        {
            return nlohmann::json::array({
                "Effect order: SSAO, rain droplets, underwater, depth of field, auto exposure, plugin effects before "
                "the tonemap (priority 95), tone mapping, color grading, plugin effects after the tonemap (130), "
                "bloom, edge detection, FXAA, chromatic aberration, vignette, film grain. With DLSS active, SSAO, "
                "depth of field and auto exposure run before the upscaler at render resolution and the rest after "
                "it at display resolution, in the same order.",
                "Anti-aliasing: MSAA (display.msaa), FXAA (postProcess.fxaa: FXAA 3.11 over the tone-mapped image, "
                "any GPU, can be combined with MSAA) or the upscaler (postProcess.upscale; quality Native = DLAA, "
                "full resolution). MSAA is ignored while the upscaler is active; FXAA still runs after it and only "
                "softens the image further.",
                "Everything here is saved with the scene (scene_save). In an exported game a player's graphics "
                "preset (Graphics.applyPreset / setMsaa) overrides the shadows and MSAA after each load.",
                "The volumetric fog is part of the post-process settings but belongs to fog_get / fog_set. Plugin "
                "post-process effects: postprocess_effect_list / postprocess_effect_set."
            });
        }

        // ------------------------------------------------------------------
        // Warnings
        // ------------------------------------------------------------------

        const char* const masterOffWarning =
            "postProcess.enabled is false: no built-in effect renders (tone mapping included) until "
            "render_settings_set {postProcess:{enabled:true}}; plugin effects still render";

        const char* const fxaaUnderUpscalerWarning =
            "postProcess.fxaa runs after DLSS / DLAA, which already anti-alias the image: FXAA only softens it further";

        std::string missingLutWarning(const std::string& path)
        {
            return "The LUT file '" + path + "' does not exist: the identity LUT is used instead";
        }

        // Every condition worth knowing about in the current state (render_settings_get).
        std::vector<std::string> stateWarnings(const types::RenderSettings& settings, const env::PostProcessSettings& live,
                                               const std::optional<services::UpscaleStatus>& status,
                                               const nlohmann::json& liveDiff)
        {
            std::vector<std::string> warnings;
            if (!live.enabled)
            {
                warnings.emplace_back(masterOffWarning);
            }
            else if (!live.toneMapping.enabled)
            {
                warnings.emplace_back("toneMapping.enabled is false: the HDR scene is shown without tone mapping and "
                                      "exposure, contrast and gamma have no effect");
            }
            if (status.has_value() && live.upscale.enabled && live.upscale.mode != ::postprocess::UpscaleMode::Off &&
                !status->dlssSupported)
            {
                warnings.emplace_back("The upscaler is enabled but DLSS is not supported on this GPU, so it stays off");
            }
            if (settings.display.msaa != types::MsaaSamples::Off && upscalerActive(live, status))
            {
                warnings.emplace_back("display.msaa is ignored while the upscaler is active (DLSS / DLAA anti-alias "
                                      "the image themselves)");
            }
            if (live.enabled && live.fxaa.enabled && upscalerActive(live, status))
            {
                warnings.emplace_back(fxaaUnderUpscalerWarning);
            }
            if (settings.postProcess.colorGrading.enabled)
            {
                for (const std::string& path : env::missingLutPaths(settings.postProcess.colorGrading))
                {
                    warnings.push_back(missingLutWarning(path));
                }
            }
            if (!liveDiff.empty())
            {
                warnings.emplace_back("The live post-process settings differ from the saved ones (see 'live'; an "
                                      "unsaved editor change or a script): render_settings_set writes both copies");
            }
            return warnings;
        }

        bool diffHas(const nlohmann::json& diff, std::string_view section, std::string_view key)
        {
            const auto it = diff.find(std::string(section));
            return it != diff.end() && it->is_object() && (key.empty() || it->contains(std::string(key)));
        }

        // Warnings about what a render_settings_set changed (`diff` = its nested diff).
        std::vector<std::string> editWarnings(const nlohmann::json& diff, const types::RenderSettings& settings,
                                              const env::PostProcessSettings& live,
                                              const std::optional<services::UpscaleStatus>& status)
        {
            std::vector<std::string> warnings;
            const auto postProcessDiff = diff.find("postProcess");
            const bool postProcessChanged = postProcessDiff != diff.end();
            if (postProcessChanged && !live.enabled)
            {
                warnings.emplace_back(masterOffWarning);
            }
            else if (postProcessChanged)
            {
                if (diffHas(*postProcessDiff, "toneMapping", "") && !live.toneMapping.enabled)
                {
                    warnings.emplace_back("toneMapping.enabled is false: exposure, contrast, gamma and the tone curve "
                                          "only show once it is true");
                }
                if (diffHas(*postProcessDiff, "toneMapping", "exposure") && live.autoExposure.enabled)
                {
                    warnings.emplace_back("autoExposure.enabled overrides toneMapping.exposure; to brighten or darken, "
                                          "change autoExposure.exposureCompensation");
                }
                // This call put FXAA on top of the upscaler when it changed FXAA, the upscaler or the master switch
                // (a bool, so diffHas - which wants a section object - cannot see it).
                if ((postProcessDiff->contains("enabled") || diffHas(*postProcessDiff, "fxaa", "") ||
                     diffHas(*postProcessDiff, "upscale", "")) &&
                    live.fxaa.enabled && upscalerActive(live, status))
                {
                    warnings.emplace_back(fxaaUnderUpscalerWarning);
                }
            }
            if (postProcessChanged && diffHas(*postProcessDiff, "upscale", "") && status.has_value() &&
                live.upscale.enabled && live.upscale.mode != ::postprocess::UpscaleMode::Off && !status->dlssSupported)
            {
                warnings.emplace_back("DLSS is not supported on this GPU: the upscaler stays off");
            }
            if (diffHas(diff, "display", "msaa") && settings.display.msaa != types::MsaaSamples::Off &&
                upscalerActive(live, status))
            {
                warnings.emplace_back("display.msaa is saved but ignored while the upscaler is active (DLSS / DLAA "
                                      "anti-alias the image themselves); turn postProcess.upscale off to use MSAA");
            }
            if (postProcessChanged && diffHas(*postProcessDiff, "colorGrading", "") &&
                settings.postProcess.colorGrading.enabled)
            {
                for (const std::string& path : env::missingLutPaths(settings.postProcess.colorGrading))
                {
                    warnings.push_back(missingLutWarning(path));
                }
            }
            return warnings;
        }

        // ------------------------------------------------------------------
        // Staging a render_settings_set
        // ------------------------------------------------------------------

        struct StagedEdit
        {
            std::vector<env::SectionValue> before; // only the sections that change
            std::vector<env::SectionValue> after;
            nlohmann::json diff = nlohmann::json::object(); // {postProcess{...}, shadows{...}, display{...}}
        };

        std::string postProcessKeys()
        {
            std::string keys = "enabled";
            env::forEachPostProcessSection([&keys](std::string_view key, const auto&, auto)
            {
                keys += ", ";
                keys += key;
            });
            return keys;
        }

        // Validates the whole request (throws ArgError, changes nothing) and returns the changed sections.
        StagedEdit stageEdit(const nlohmann::json& args, const types::RenderSettings& authored)
        {
            env::rejectUnknownArgs(args, {"postProcess", "shadows", "display"}, "render_settings_set");
            if (!args.is_object() || args.empty())
            {
                throw ArgError("nothing to change: pass postProcess, shadows and/or display (render_settings_get "
                               "lists every key with its range)");
            }

            StagedEdit edit;
            const std::optional<fs::path> root = tryTerrainProjectRoot();

            if (args.contains("postProcess"))
            {
                const nlohmann::json& postProcess = args.at("postProcess");
                if (!postProcess.is_object())
                {
                    throw ArgError("'postProcess' must be an object (got " + postProcess.dump(-1, ' ', false,
                                   nlohmann::json::error_handler_t::replace) + ")");
                }
                for (auto it = postProcess.begin(); it != postProcess.end(); ++it)
                {
                    const std::string& key = it.key();
                    bool known = key == "enabled";
                    env::forEachPostProcessSection([&known, &key](std::string_view section, const auto&, auto)
                    {
                        known = known || section == key;
                    });
                    if (known)
                    {
                        continue;
                    }
                    if (const std::optional<std::string_view> reason = env::unsettablePostProcessKey(key))
                    {
                        throw ArgError("'postProcess." + key + "' cannot be set here: " + std::string(*reason));
                    }
                    throw ArgError("unknown postProcess key '" + key + "'; valid keys: " + postProcessKeys());
                }

                nlohmann::json postProcessDiff = nlohmann::json::object();
                if (postProcess.contains("enabled"))
                {
                    const env::PostProcessMasterSettings before = masterOf(authored.postProcess);
                    const env::PostProcessMasterSettings after = env::strictPatch(
                        env::postProcessMasterTable(), before, nlohmann::json{{"enabled", postProcess.at("enabled")}},
                        "postProcess");
                    if (after.enabled != before.enabled)
                    {
                        edit.before.emplace_back(before);
                        edit.after.emplace_back(after);
                        postProcessDiff["enabled"] = after.enabled;
                    }
                }
                env::forEachPostProcessSection([&](std::string_view key, const auto& table, auto member)
                {
                    const auto found = postProcess.find(std::string(key));
                    if (found == postProcess.end())
                    {
                        return;
                    }
                    const auto& before = authored.postProcess.*member;
                    using S = std::decay_t<decltype(before)>;
                    const std::string prefix = "postProcess." + std::string(key);
                    nlohmann::json patch = *found;
                    if constexpr (std::is_same_v<S, env::ColorGradingSettings>)
                    {
                        patch = env::resolveLutPaths(patch, before, prefix);
                    }
                    const S after = env::strictPatch(table, before, patch, prefix);
                    nlohmann::json diff = env::diffJson(table, before, after);
                    if (diff.empty())
                    {
                        return;
                    }
                    if constexpr (std::is_same_v<S, env::ColorGradingSettings>)
                    {
                        env::reportLutPaths(diff, root);
                    }
                    edit.before.emplace_back(before);
                    edit.after.emplace_back(after);
                    postProcessDiff[std::string(key)] = std::move(diff);
                });
                if (!postProcessDiff.empty())
                {
                    edit.diff["postProcess"] = std::move(postProcessDiff);
                }
            }

            if (args.contains("shadows"))
            {
                const env::ShadowQualitySettings before = env::shadowQualityOf(authored.shadows);
                const env::ShadowQualitySettings after =
                    env::strictPatch(env::shadowTable(), before, args.at("shadows"), "shadows");
                nlohmann::json diff = env::diffJson(env::shadowTable(), before, after);
                if (!diff.empty())
                {
                    edit.before.emplace_back(before);
                    edit.after.emplace_back(after);
                    edit.diff["shadows"] = std::move(diff);
                }
            }

            if (args.contains("display"))
            {
                const env::DisplayAntiAliasingSettings before = displayOf(authored);
                const env::DisplayAntiAliasingSettings after =
                    env::strictPatch(env::displayTable(), before, args.at("display"), "display");
                nlohmann::json diff = env::diffJson(env::displayTable(), before, after);
                if (!diff.empty())
                {
                    edit.before.emplace_back(before);
                    edit.after.emplace_back(after);
                    edit.diff["display"] = std::move(diff);
                }
            }
            return edit;
        }

        std::string lookSummary(const types::RenderSettings& settings, const env::PostProcessSettings& live,
                                const std::optional<services::UpscaleStatus>& status)
        {
            int on = 0;
            int total = 0;
            env::forEachPostProcessSection([&](std::string_view key, const auto&, auto member)
            {
                if (key != "upscale")
                {
                    ++total;
                    on += (live.*member).enabled ? 1 : 0;
                }
            });
            std::string summary = live.enabled
                ? std::format("Post-processing on ({} of {} effects enabled), tone mapping {}", on, total,
                              live.toneMapping.enabled ? enumText(env::toneMappingModeNames, live.toneMapping.mode)
                                                       : std::string("off"))
                : std::string("Post-processing off");
            summary += ", anti-aliasing " + antiAliasingText(settings, live, status) + ", shadows " +
                       (types::shadowsRendered(settings.shadows) ? enumText(env::shadowQualityNames, settings.shadows.quality)
                                                                 : std::string("off")) + ".";
            return summary;
        }

        // ------------------------------------------------------------------
        // render_settings_get / render_settings_set
        // ------------------------------------------------------------------

        void registerRenderSettingsGet(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "render_settings_get";
            tool.title = "Get render settings";
            tool.description =
                "Read the scene's look: postProcess {enabled (master switch), toneMapping, autoExposure, bloom, "
                "colorGrading, ssao, vignette, chromaticAberration, filmGrain, depthOfField, edgeDetection, fxaa, "
                "upscale (DLSS / DLAA)}, shadows (quality and filtering) and display {msaa}, as saved with the scene - "
                "the same shape render_settings_set takes, so any part can be passed back. Also: live (post-process "
                "keys whose live value differs from the saved one), effective (what renders now: each effect, the "
                "exposure source, the active anti-aliasing, the upscaler status), warnings, notes (effect order, "
                "anti-aliasing options) and ranges = every key with its type and range (includeRanges:false omits "
                "them). Volumetric fog: fog_get. Plugin effects: postprocess_effect_list.";
            tool.inputSchema = closedObject({
                {"includeRanges", schema::boolean("Include 'ranges' (the valid render_settings_set values). Default true.")}
            });
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"includeRanges"}, "render_settings_get");
                const ArgReader reader(args);
                const bool includeRanges = reader.optBool("includeRanges", true);

                const types::RenderSettings settings = authoredSettings();
                const env::PostProcessSettings live = livePostProcess();
                const std::optional<services::UpscaleStatus> status = upscaleStatus();
                const std::optional<fs::path> root = tryTerrainProjectRoot();

                nlohmann::json out = settingsJson(settings, root);
                const nlohmann::json liveDiff = liveDiffJson(settings.postProcess, live, root);
                out["live"] = liveDiff;
                out["effective"] = effectiveJson(settings, live, status);
                out["warnings"] = stateWarnings(settings, live, status, liveDiff);
                out["notes"] = notesJson();
                if (includeRanges)
                {
                    out["ranges"] = rangesJson();
                }
                std::string summary = lookSummary(settings, live, status);
                if (!out["warnings"].empty())
                {
                    summary += " " + std::to_string(out["warnings"].size()) + " warning(s).";
                }
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }

        nlohmann::json setSchema()
        {
            nlohmann::json postProcess = env::schemaJson(env::postProcessMasterTable(), "")["properties"];
            env::forEachPostProcessSection([&postProcess](std::string_view key, const auto& table, auto)
            {
                postProcess[std::string(key)] =
                    env::schemaJson(table, "The " + std::string(table.name) + " settings to change (partial).");
            });
            nlohmann::json postProcessSchema = closedObject(std::move(postProcess));
            postProcessSchema["description"] =
                "Post-process settings to change: enabled (the master switch) and/or effect objects (partial).";
            return closedObject({
                {"postProcess", std::move(postProcessSchema)},
                {"shadows", env::schemaJson(env::shadowTable(), "Shadow settings to change (partial).")},
                {"display", env::schemaJson(env::displayTable(), "Anti-aliasing settings to change (partial).")}
            });
        }

        void registerRenderSettingsSet(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "render_settings_set";
            tool.title = "Set render settings";
            tool.description =
                "Change the scene's look: pass any part of render_settings_get's postProcess, shadows and display "
                "(partial update at every level; an unknown key, a wrong type or an out-of-range value fails the "
                "whole call, changes nothing and names the valid range). Nothing built-in renders unless "
                "postProcess.enabled is true, and exposure / the tone curve need toneMapping.enabled. While "
                "autoExposure.enabled, change autoExposure.exposureCompensation rather than toneMapping.exposure. "
                "Anti-aliasing: display.msaa, postProcess.fxaa {enabled:true} (any GPU), or postProcess.upscale "
                "{enabled:true, quality:\"Native\"} for DLAA (DLSS needs an RTX GPU; MSAA is ignored while it runs, "
                "FXAA still runs after it). colorGrading LUT paths are .cube files relative to the project (\"\" "
                "clears). A shadows change re-applies the scene's culling and terrain settings too, like the Render "
                "window's Apply. Saved with the scene on scene_save; one undo step; refused in Play mode. Returns "
                "{changed, diff, effective, warnings}. Volumetric fog: fog_set.";
            tool.inputSchema = setSchema();
            tool.timeout = renderSettingsTimeout;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                if (auto blocked = env::editBlocker("change the render settings"))
                {
                    return ToolResult::error(*blocked);
                }

                const StagedEdit edit = stageEdit(args, authoredSettings());
                if (!edit.after.empty())
                {
                    undo::UndoRecorder recorder("MCP: Set render settings");
                    try
                    {
                        recorder.add(std::make_unique<undo::RenderSettingsSectionUndo>(edit.before, edit.after));
                        env::writeSections(edit.after);
                    }
                    catch (const std::exception& e)
                    {
                        recorder.rollback();
                        return ToolResult::error(std::string("The render settings were not changed (rolled back): ") +
                                                 e.what());
                    }
                    recorder.push();
                }

                const types::RenderSettings settings = authoredSettings();
                const env::PostProcessSettings live = livePostProcess();
                const std::optional<services::UpscaleStatus> status = upscaleStatus();
                nlohmann::json out{
                    {"changed", !edit.after.empty()},
                    {"diff", edit.diff},
                    {"effective", effectiveJson(settings, live, status)},
                    {"warnings", editWarnings(edit.diff, settings, live, status)}
                };
                std::string summary = edit.after.empty()
                    ? std::string("Nothing changed.")
                    : "Changed " + std::to_string(edit.after.size()) + " section(s). " + lookSummary(settings, live, status);
                if (!out["warnings"].empty())
                {
                    summary += " " + std::to_string(out["warnings"].size()) + " warning(s).";
                }
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // postprocess_effect_list / postprocess_effect_set
        // ------------------------------------------------------------------

        std::vector<PluginPostProcessEffectInfo> pluginEffectsOrThrow()
        {
            try
            {
                return env::listPluginEffects();
            }
            catch (const std::runtime_error& e)
            {
                throw std::runtime_error(std::string("Plugin post-process effects are unavailable: ") + e.what());
            }
        }

        std::string effectLabel(const PluginPostProcessEffectInfo& effect)
        {
            return effect.plugin + "/" + (effect.name.empty() ? "#" + std::to_string(effect.id) : effect.name);
        }

        std::string effectList(const std::vector<PluginPostProcessEffectInfo>& effects)
        {
            std::string out;
            for (const PluginPostProcessEffectInfo& effect : effects)
            {
                out += out.empty() ? "" : ", ";
                out += effectLabel(effect) + " (id " + std::to_string(effect.id) + ")";
            }
            return out.empty() ? std::string("none (no loaded plugin registers one)") : out;
        }

        // The effect an id or plugin + name names: exact name first, then a unique case-insensitive match.
        const PluginPostProcessEffectInfo& findEffect(const std::vector<PluginPostProcessEffectInfo>& effects,
                                                      const ArgReader& reader)
        {
            if (reader.has("id"))
            {
                if (reader.has("plugin") || reader.has("name"))
                {
                    throw ArgError("pass either 'id' or 'plugin' + 'name', not both");
                }
                const int64_t id = reader.requireInt("id");
                for (const PluginPostProcessEffectInfo& effect : effects)
                {
                    if (id > 0 && effect.id == static_cast<uint64_t>(id))
                    {
                        return effect;
                    }
                }
                throw ArgError("no plugin post-process effect has id " + std::to_string(id) + "; effects: " +
                               effectList(effects));
            }
            if (!reader.has("name"))
            {
                throw ArgError("pass 'id' or 'name' (and optionally 'plugin'); effects: " + effectList(effects));
            }
            const std::string name = reader.requireString("name");
            const std::string plugin = reader.optString("plugin");
            if (name.empty())
            {
                throw ArgError("'name' is empty; pass the effect's name or its 'id'; effects: " + effectList(effects));
            }

            const auto collect = [&](bool exact)
            {
                std::vector<const PluginPostProcessEffectInfo*> matches;
                for (const PluginPostProcessEffectInfo& effect : effects)
                {
                    const bool nameMatches = exact ? effect.name == name : env::detail::equalsIgnoreCase(effect.name, name);
                    const bool pluginMatches = plugin.empty() ||
                        (exact ? effect.plugin == plugin : env::detail::equalsIgnoreCase(effect.plugin, plugin));
                    if (nameMatches && pluginMatches)
                    {
                        matches.push_back(&effect);
                    }
                }
                return matches;
            };
            std::vector<const PluginPostProcessEffectInfo*> matches = collect(true);
            if (matches.empty())
            {
                matches = collect(false);
            }
            if (matches.size() == 1)
            {
                return *matches.front();
            }
            if (matches.empty())
            {
                throw ArgError("no plugin post-process effect is named '" + name + "'" +
                               (plugin.empty() ? std::string() : " in plugin '" + plugin + "'") + "; effects: " +
                               effectList(effects));
            }
            std::vector<PluginPostProcessEffectInfo> ambiguous;
            for (const PluginPostProcessEffectInfo* match : matches)
            {
                ambiguous.push_back(*match);
            }
            throw ArgError("'" + name + "' names " + std::to_string(matches.size()) +
                           " effects; pass 'id' (or 'plugin'): " + effectList(ambiguous));
        }

        void registerPostprocessEffectList(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "postprocess_effect_list";
            tool.title = "List plugin post-process effects";
            tool.description =
                "List the post-process effects plugins registered (VK-1409): {id, plugin, name, priority, stage "
                "(beforeTonemap = on linear HDR, afterTonemap = on the tone-mapped image), enabled (requested), "
                "pluginActive (false while the plugin is deactivated for this scene), rendering}. Toggle one with "
                "postprocess_effect_set. Plugin effects render even when postProcess.enabled is false; their state "
                "is per session (the plugin decides it at startup) and is not saved with the scene.";
            tool.inputSchema = closedObject(nlohmann::json::object());
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {}, "postprocess_effect_list");
                const std::vector<PluginPostProcessEffectInfo> effects = pluginEffectsOrThrow();
                nlohmann::json list = nlohmann::json::array();
                std::size_t rendering = 0;
                for (const PluginPostProcessEffectInfo& effect : effects)
                {
                    list.push_back(env::pluginEffectJson(effect));
                    rendering += effect.requestedEnabled && effect.pluginActive ? 1 : 0;
                }
                nlohmann::json out{{"effects", std::move(list)}};
                const std::string summary = std::to_string(effects.size()) + " plugin post-process effect(s), " +
                                            std::to_string(rendering) + " rendering.";
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }

        void registerPostprocessEffectSet(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "postprocess_effect_set";
            tool.title = "Enable or disable a plugin post-process effect";
            tool.description =
                "Enable or disable one plugin post-process effect, named by its id or by name (and optionally "
                "plugin) from postprocess_effect_list. The request is kept while the plugin is deactivated for this "
                "scene and applies when it is re-activated. Per session, like the plugin's own toggle (not saved "
                "with the scene); one undo step; refused in Play mode. Returns {changed, effect, warnings}.";
            tool.inputSchema = closedObject({
                {"id", schema::integer("The effect's id from postprocess_effect_list.")},
                {"plugin", schema::string("The owning plugin's name, to tell effects with the same name apart.")},
                {"name", schema::string("The effect's name from postprocess_effect_list.")},
                {"enabled", schema::boolean("true = render the effect, false = bypass it.")}
            }, {"enabled"});
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                if (auto blocked = env::editBlocker("toggle a plugin post-process effect"))
                {
                    return ToolResult::error(*blocked);
                }
                env::rejectUnknownArgs(args, {"id", "plugin", "name", "enabled"}, "postprocess_effect_set");
                const ArgReader reader(args);
                const bool enabled = reader.requireBool("enabled");
                const std::vector<PluginPostProcessEffectInfo> effects = pluginEffectsOrThrow();
                const PluginPostProcessEffectInfo target = findEffect(effects, reader);
                const std::string label = effectLabel(target);

                const bool changed = target.requestedEnabled != enabled;
                if (changed)
                {
                    undo::UndoRecorder recorder(std::string(enabled ? "MCP: Enable " : "MCP: Disable ") + label);
                    try
                    {
                        recorder.add(std::make_unique<undo::PluginPostProcessEffectUndo>(target.id, label,
                                                                                         target.requestedEnabled, enabled));
                        if (!env::setPluginEffectEnabled(target.id, enabled))
                        {
                            throw std::runtime_error("the effect " + label + " no longer exists");
                        }
                    }
                    catch (const std::exception& e)
                    {
                        recorder.rollback();
                        return ToolResult::error(std::string("The effect was not changed (rolled back): ") + e.what());
                    }
                    recorder.push();
                }

                PluginPostProcessEffectInfo current = target;
                current.requestedEnabled = enabled;
                for (const PluginPostProcessEffectInfo& effect : pluginEffectsOrThrow())
                {
                    if (effect.id == target.id)
                    {
                        current = effect;
                    }
                }
                std::vector<std::string> warnings;
                if (!current.pluginActive)
                {
                    warnings.push_back("The plugin '" + current.plugin + "' is deactivated for this scene: the effect "
                                       "stays off until the plugin is re-activated; the request is kept");
                }
                nlohmann::json out{
                    {"changed", changed},
                    {"effect", env::pluginEffectJson(current)},
                    {"warnings", warnings}
                };
                const std::string summary = changed
                    ? std::string(enabled ? "Enabled " : "Disabled ") + label + "."
                    : label + " was already " + (enabled ? "enabled." : "disabled.");
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }
    }

    void registerRenderSettingsTools(ToolRegistry& registry, const ToolContext&)
    {
        registerRenderSettingsGet(registry);
        registerRenderSettingsSet(registry);
        registerPostprocessEffectList(registry);
        registerPostprocessEffectSet(registry);
    }
}

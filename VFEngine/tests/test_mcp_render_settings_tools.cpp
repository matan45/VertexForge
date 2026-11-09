#include <doctest.h>

#include "dispatch/MainThreadQueue.hpp"
#include "protocol/ArgReader.hpp"
#include "protocol/ToolRegistry.hpp"
#include "tools/CoreTools.hpp"
#include "tools/RenderSettingsToolSupport.hpp"
#include "undo/EntityIdRemap.hpp"

#include "asset/AssetDatabase.hpp"

#include "events/EventDispatcher.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/editor/PluginPostProcessEffectEvents.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "events/project/ApplicationEvents.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/render/PostProcessEvents.hpp"
#include "events/render/RenderEvents.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "events/scene/SceneSettingsEvents.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

// VK-1655: the MCP render-settings tools against a fake engine that keeps both copies of the look: the
// scene's authored types::RenderSettings (Get/SetRenderSettings) and the live post-process settings, plus
// the shadow / display applies, the upscaler status, the plugin post-process effects and an undo stack. The
// log records every mutating event and notification in dispatch order.
namespace
{
    namespace fs = std::filesystem;
    namespace env = mcp::tools::env;
    using events::editor::PluginPostProcessEffectInfo;

    struct DispatcherScope
    {
        DispatcherScope()
        {
            events::EventDispatcher::instance().clear();
            mcp::undo::EntityIdRemap::instance().clear();
            asset::AssetDatabase::instance().clear();
        }

        ~DispatcherScope()
        {
            events::EventDispatcher::instance().clear();
            mcp::undo::EntityIdRemap::instance().clear();
            asset::AssetDatabase::instance().clear();
        }
    };

    fs::path makeTestDirectory(const char* name)
    {
        auto directory = fs::temp_directory_path() / name;
        std::error_code ec;
        fs::remove_all(directory, ec);
        fs::create_directories(directory);
        return fs::weakly_canonical(directory);
    }

    void touch(const fs::path& path)
    {
        fs::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary);
        stream << "LUT_3D_SIZE 2\n";
    }

    bool contains(const std::string& text, const std::string& needle)
    {
        return text.find(needle) != std::string::npos;
    }

    bool anyContains(const nlohmann::json& warnings, const std::string& needle)
    {
        for (const nlohmann::json& warning : warnings)
        {
            if (warning.is_string() && contains(warning.get<std::string>(), needle))
            {
                return true;
            }
        }
        return false;
    }

    std::string logText(const std::vector<std::string>& log)
    {
        std::string text;
        for (const std::string& entry : log)
        {
            text += text.empty() ? entry : ", " + entry;
        }
        return "[" + text + "]";
    }

    void checkLog(const std::vector<std::string>& actual, const std::vector<std::string>& expected)
    {
        INFO("expected " << logText(expected));
        INFO("actual   " << logText(actual));
        CHECK(actual == expected);
    }

    nlohmann::json schemaValue(const nlohmann::json& schema, const char* key)
    {
        return schema.is_object() && schema.contains(key) ? schema.at(key) : nlohmann::json(nullptr);
    }

    std::size_t batchSize(const std::shared_ptr<services::IUndoableCommand>& entry)
    {
        const auto* batch = dynamic_cast<const services::BatchUndoCommand*>(entry.get());
        return batch != nullptr ? batch->getCommandCount() : 0;
    }

    const char* sectionText(events::scene::RenderSettingsSection section)
    {
        switch (section)
        {
        case events::scene::RenderSettingsSection::All: return "All";
        case events::scene::RenderSettingsSection::Atmosphere: return "Atmosphere";
        case events::scene::RenderSettingsSection::Cloud: return "Cloud";
        case events::scene::RenderSettingsSection::VolumetricFog: return "VolumetricFog";
        case events::scene::RenderSettingsSection::GI: return "GI";
        case events::scene::RenderSettingsSection::IBL: return "IBL";
        case events::scene::RenderSettingsSection::PostProcess: return "PostProcess";
        case events::scene::RenderSettingsSection::Shadows: return "Shadows";
        case events::scene::RenderSettingsSection::Display: return "Display";
        }
        return "?";
    }

    PluginPostProcessEffectInfo effectInfo(uint64_t id, std::string plugin, std::string name, uint32_t priority,
                                           bool enabled, bool active = true)
    {
        PluginPostProcessEffectInfo info;
        info.id = id;
        info.plugin = std::move(plugin);
        info.name = std::move(name);
        info.priority = priority;
        info.requestedEnabled = enabled;
        info.pluginActive = active;
        return info;
    }

    struct FakeRenderEngine
    {
        fs::path assets;
        bool playMode = false;
        bool loadPending = false;
        bool storeSucceeds = true;
        bool dlssSupported = true;
        bool effectSetSucceeds = true; // false: the effect vanished (its plugin unloaded) after the listing

        types::RenderSettings scene = types::RenderSettings::createDefault();
        ::postprocess::PostProcessSettings live = types::RenderSettings::createDefault().postProcess;
        std::vector<PluginPostProcessEffectInfo> effects;

        std::optional<types::RenderSettings> appliedShadows;
        std::optional<events::application::ApplyDisplaySettingsNotification> appliedDisplay;
        std::vector<std::shared_ptr<services::IUndoableCommand>> undoStack;
        std::vector<std::string> log;

        void registerHandlers()
        {
            auto& d = events::EventDispatcher::instance();

            d.registerQueryHandler<events::editor::IsPlayModeQuery>(
                [this](const events::editor::IsPlayModeQuery&) { return playMode; });
            d.registerQueryHandler<events::scene::IsSceneLoadPendingQuery>(
                [this](const events::scene::IsSceneLoadPendingQuery&) { return loadPending; });
            d.registerQueryHandler<events::project::GetCurrentProjectQuery>(
                [this](const events::project::GetCurrentProjectQuery&) -> std::optional<config::ProjectConfig>
                {
                    config::ProjectConfig config;
                    config.projectName = "McpRenderSettings";
                    config.workingDirectory = assets.string();
                    return config;
                });

            // The authored copy.
            d.registerQueryHandler<events::scene::GetRenderSettingsQuery>(
                [this](const events::scene::GetRenderSettingsQuery&) { return scene; });
            d.registerCommandHandler<events::scene::SetRenderSettingsCommand>(
                [this](const events::scene::SetRenderSettingsCommand& command)
                {
                    log.push_back("SetRenderSettings");
                    if (storeSucceeds)
                    {
                        scene = command.settings;
                    }
                    return storeSucceeds;
                });

            // The live copies.
            d.registerQueryHandler<events::postprocess::GetPostProcessSettingsQuery>(
                [this](const events::postprocess::GetPostProcessSettingsQuery&) { return live; });
            d.registerCommandHandler<events::postprocess::ApplyPostProcessSettingsCommand>(
                [this](const events::postprocess::ApplyPostProcessSettingsCommand& command)
                {
                    log.push_back("ApplyPostProcessSettings");
                    live = command.settings;
                });
            d.registerCommandHandler<events::render::ApplyShadowSettingsCommand>(
                [this](const events::render::ApplyShadowSettingsCommand& command)
                {
                    log.push_back("ApplyShadowSettings");
                    appliedShadows = command.settings;
                });
            d.subscribe<events::application::ApplyDisplaySettingsNotification>(
                [this](const events::application::ApplyDisplaySettingsNotification& notification)
                {
                    log.push_back("ApplyDisplaySettings");
                    appliedDisplay = notification;
                });
            // UpscaleManager::applySettings: DLSS and Auto resolve to DLSS where it is supported.
            d.registerQueryHandler<events::postprocess::GetUpscaleStatusQuery>(
                [this](const events::postprocess::GetUpscaleStatusQuery&)
                {
                    services::UpscaleStatus status;
                    status.streamlineAvailable = dlssSupported;
                    status.dlssSupported = dlssSupported;
                    const bool active = live.upscale.enabled && live.upscale.mode != ::postprocess::UpscaleMode::Off &&
                                        dlssSupported;
                    status.activeMode = active ? ::postprocess::UpscaleMode::DLSS : ::postprocess::UpscaleMode::Off;
                    return status;
                });

            // Plugin post-process effects (PluginPostProcessEffectHandler).
            d.registerQueryHandler<events::editor::ListPluginPostProcessEffectsQuery>(
                [this](const events::editor::ListPluginPostProcessEffectsQuery&) { return effects; });
            d.registerCommandHandler<events::editor::SetPluginPostProcessEffectEnabledCommand>(
                [this](const events::editor::SetPluginPostProcessEffectEnabledCommand& command)
                {
                    log.push_back("SetPluginEffect:" + std::to_string(command.id) + "=" +
                                  (command.enabled ? "true" : "false"));
                    if (!effectSetSucceeds)
                    {
                        return false;
                    }
                    for (PluginPostProcessEffectInfo& effect : effects)
                    {
                        if (effect.id == command.id)
                        {
                            effect.requestedEnabled = command.enabled;
                            return true;
                        }
                    }
                    return false;
                });

            d.registerCommandHandler<events::undoredo::PushUndoableCommand>(
                [this](const events::undoredo::PushUndoableCommand& command)
                {
                    log.push_back("PushUndo");
                    undoStack.push_back(command.command);
                });

            d.subscribe<events::scene::RenderSettingsChangedNotification>(
                [this](const events::scene::RenderSettingsChangedNotification& notification)
                {
                    log.push_back(std::string("RenderSettingsChanged:") + sectionText(notification.section));
                });
        }
    };

    struct RenderSettingsToolFixture
    {
        DispatcherScope dispatcherScope;
        FakeRenderEngine engine;
        mcp::MainThreadQueue queue;
        mcp::ToolRegistry registry;

        RenderSettingsToolFixture()
        {
            engine.assets = makeTestDirectory("VertexForge_McpRenderSettingsTools");
            engine.registerHandlers();
            const mcp::tools::ToolContext context{queue};
            mcp::tools::registerRenderSettingsTools(registry, context);
        }

        ~RenderSettingsToolFixture()
        {
            std::error_code ec;
            fs::remove_all(engine.assets, ec);
        }

        const mcp::ToolDef& tool(const char* name) const
        {
            auto found = registry.find(name);
            REQUIRE(found != nullptr);
            return *found;
        }

        mcp::ToolResult call(const char* name, const nlohmann::json& args)
        {
            const mcp::ToolDef& def = tool(name);
            REQUIRE(def.affinity == mcp::ThreadAffinity::Main);
            return def.handler(args);
        }

        nlohmann::json callOk(const char* name, const nlohmann::json& args)
        {
            const mcp::ToolResult result = call(name, args);
            INFO(name << " failed: " << result.text);
            REQUIRE_FALSE(result.isError);
            return result.structured;
        }

        std::string argError(const char* name, const nlohmann::json& args)
        {
            try
            {
                call(name, args);
            }
            catch (const mcp::ArgError& e)
            {
                return e.what();
            }
            catch (const std::exception& e)
            {
                return std::string("<not an ArgError: ") + e.what() + ">";
            }
            return "<no error>";
        }

        void addPluginEffects()
        {
            engine.effects = {
                effectInfo(1, "PostProcessFX", "PPFX_Exposure", 95, true),
                effectInfo(2, "PostProcessFX", "PPFX_Grade", 130, true),
                effectInfo(3, "OtherFX", "PPFX_Grade", 140, false),
                effectInfo(4, "OtherFX", "", 120, true)
            };
        }
    };
}

TEST_CASE("mcp render settings tools: four Main-affinity tools with closed schemas, annotations and timeouts")
{
    RenderSettingsToolFixture fixture;
    CHECK(fixture.registry.size() == 4);

    struct Expected
    {
        const char* name;
        bool readOnly;
        long long timeoutMs;
    };
    for (const Expected& entry : {Expected{"render_settings_get", true, 10000}, Expected{"render_settings_set", false, 30000},
                                  Expected{"postprocess_effect_list", true, 10000},
                                  Expected{"postprocess_effect_set", false, 10000}})
    {
        CAPTURE(entry.name);
        const mcp::ToolDef& def = fixture.tool(entry.name);
        CHECK(def.affinity == mcp::ThreadAffinity::Main);
        CHECK(def.readOnly == entry.readOnly);
        CHECK_FALSE(def.destructive);
        CHECK(def.timeout.count() == entry.timeoutMs);
        CHECK(schemaValue(def.inputSchema, "type") == "object");
        CHECK(schemaValue(def.inputSchema, "additionalProperties") == false);
    }

    // Every nested object is closed; the fog and the runtime-driven effects are not offered.
    nlohmann::json schema = fixture.tool("render_settings_set").inputSchema;
    nlohmann::json postProcess = schema["properties"]["postProcess"];
    CHECK(schemaValue(postProcess, "additionalProperties") == false);
    for (const char* key : {"enabled", "toneMapping", "autoExposure", "bloom", "colorGrading", "ssao", "vignette",
                            "chromaticAberration", "filmGrain", "depthOfField", "edgeDetection", "fxaa", "upscale"})
    {
        CAPTURE(key);
        CHECK(postProcess["properties"].contains(key));
    }
    for (const char* key : {"volumetricFog", "underwater", "rainDroplets", "ssr", "frameGen", "reflex"})
    {
        CAPTURE(key);
        CHECK_FALSE(postProcess["properties"].contains(key));
    }
    CHECK(schemaValue(postProcess["properties"]["bloom"], "additionalProperties") == false);
    CHECK(postProcess["properties"]["bloom"]["properties"].contains("intensity"));
    CHECK(postProcess["properties"]["toneMapping"]["properties"]["mode"]["enum"] ==
          nlohmann::json::array({"ACES", "Reinhard", "Uncharted2", "Linear", "GranTurismo", "AgX", "KhronosPBRNeutral"}));
    CHECK(postProcess["properties"]["upscale"]["properties"]["quality"]["enum"] ==
          nlohmann::json::array({"Native", "Quality", "Balanced", "Performance", "UltraPerformance"}));
    CHECK(postProcess["properties"]["fxaa"]["properties"]["quality"]["enum"] ==
          nlohmann::json::array({"Low", "Medium", "High", "Ultra"}));
    CHECK(schema["properties"]["shadows"]["properties"]["quality"]["enum"] ==
          nlohmann::json::array({"Off", "Low", "Medium", "High", "Ultra"}));
    CHECK(schema["properties"]["display"]["properties"]["msaa"]["enum"] == nlohmann::json::array({"Off", "X2", "X4", "X8"}));
    CHECK_FALSE(schema["properties"]["shadows"]["properties"].contains("clipmapLevelCount"));
    CHECK_FALSE(schema["properties"]["display"]["properties"].contains("presentMode"));

    CHECK(fixture.tool("postprocess_effect_set").inputSchema["required"] == nlohmann::json::array({"enabled"}));
}

TEST_CASE("mcp render settings tools: registering the tools dispatches nothing")
{
    DispatcherScope scope; // no handlers at all: any dispatch would throw
    mcp::MainThreadQueue queue;
    mcp::ToolRegistry registry;
    const mcp::tools::ToolContext context{queue};
    CHECK_NOTHROW(mcp::tools::registerRenderSettingsTools(registry, context));
    CHECK(registry.size() == 4);
}

TEST_CASE("mcp render settings tools: invalid arguments fail the whole call and change nothing")
{
    RenderSettingsToolFixture fixture;
    const types::RenderSettings sceneBefore = fixture.engine.scene;

    CHECK(contains(fixture.argError("render_settings_set", {{"bloom", {{"intensity", 1}}}}),
                   "unknown argument 'bloom' for render_settings_set; valid arguments: postProcess, shadows, display"));
    CHECK(contains(fixture.argError("render_settings_set", nlohmann::json::object()), "nothing to change"));
    CHECK(contains(fixture.argError("render_settings_set", {{"postProcess", 5}}), "'postProcess' must be an object (got 5)"));
    CHECK(contains(fixture.argError("render_settings_set", {{"postProcess", {{"glow", true}}}}),
                   "unknown postProcess key 'glow'; valid keys: enabled, toneMapping, autoExposure, bloom, colorGrading, "
                   "ssao, vignette, chromaticAberration, filmGrain, depthOfField, edgeDetection, fxaa, upscale"));
    CHECK(contains(fixture.argError("render_settings_set", {{"postProcess", {{"bloom", {{"glow", 1}}}}}}),
                   "unknown postProcess.bloom key 'glow'; valid keys: enabled, threshold, intensity, radius, passes"));
    CHECK(contains(fixture.argError("render_settings_set", {{"postProcess", {{"volumetricFog", {{"enabled", true}}}}}}),
                   "'postProcess.volumetricFog' cannot be set here: set the volumetric fog with fog_set"));
    CHECK(contains(fixture.argError("render_settings_set", {{"postProcess", {{"ssr", {{"enabled", true}}}}}}),
                   "not saved with the scene"));

    // The range is named in the error, so the agent can correct itself.
    CHECK(contains(fixture.argError("render_settings_set", {{"postProcess", {{"bloom", {{"intensity", 99}}}}}}),
                   "'postProcess.bloom.intensity' must be between 0.0 and 5.0 (got 99)"));
    CHECK(contains(fixture.argError("render_settings_set", {{"postProcess", {{"toneMapping", {{"exposure", 0}}}}}}),
                   "'postProcess.toneMapping.exposure' must be between 0.01 and 20.0 (got 0)"));
    CHECK(contains(fixture.argError("render_settings_set", {{"postProcess", {{"ssao", {{"kernelSize", 65}}}}}}),
                   "'postProcess.ssao.kernelSize' must be between 8 and 64 (got 65)"));
    CHECK(contains(fixture.argError("render_settings_set", {{"shadows", {{"shadowIntensity", 2}}}}),
                   "'shadows.shadowIntensity' must be between 0.0 and 1.0 (got 2)"));
    CHECK(contains(fixture.argError("render_settings_set", {{"postProcess", {{"fxaa", {{"edgeThreshold", 0.5}}}}}}),
                   "'postProcess.fxaa.edgeThreshold' must be between 0.063 and 0.333 (got 0.5)"));

    // Types and enum names.
    CHECK(contains(fixture.argError("render_settings_set", {{"postProcess", {{"enabled", "yes"}}}}),
                   "'postProcess.enabled' must be a boolean (got \"yes\")"));
    CHECK(contains(fixture.argError("render_settings_set", {{"postProcess", {{"toneMapping", {{"mode", "Filmic"}}}}}}),
                   "'postProcess.toneMapping.mode' must be one of ACES, Reinhard, Uncharted2, Linear, GranTurismo, AgX, "
                   "KhronosPBRNeutral (got \"Filmic\")"));
    CHECK(contains(fixture.argError("render_settings_set", {{"shadows", {{"quality", "Epic"}}}}),
                   "'shadows.quality' must be one of Off, Low, Medium, High, Ultra (got \"Epic\")"));
    CHECK(contains(fixture.argError("render_settings_set", {{"postProcess", {{"fxaa", {{"quality", "Extreme"}}}}}}),
                   "'postProcess.fxaa.quality' must be one of Low, Medium, High, Ultra (got \"Extreme\")"));
    CHECK(contains(fixture.argError("render_settings_set", {{"display", {{"msaa", "X16"}}}}),
                   "'display.msaa' must be one of Off, X2, X4, X8 (got \"X16\")"));
    CHECK(contains(fixture.argError("render_settings_set", {{"display", {{"presentMode", 0}}}}),
                   "unknown display key 'presentMode'; valid keys: msaa"));

    // A valid section next to an invalid one: nothing at all is written.
    CHECK(contains(fixture.argError("render_settings_set", {{"postProcess", {{"enabled", true}}},
                                                            {"shadows", {{"quality", "Epic"}}}}),
                   "'shadows.quality'"));
    CHECK(contains(fixture.argError("render_settings_get", {{"foo", 1}}), "unknown argument 'foo'"));
    CHECK(contains(fixture.argError("postprocess_effect_list", {{"foo", 1}}), "valid arguments: none"));

    CHECK(fixture.engine.log.empty());
    CHECK(fixture.engine.undoStack.empty());
    CHECK(env::toJson(env::bloomTable(), fixture.engine.scene.postProcess.bloom) ==
          env::toJson(env::bloomTable(), sceneBefore.postProcess.bloom));
    CHECK(fixture.engine.scene.postProcess.enabled == sceneBefore.postProcess.enabled);
}

TEST_CASE("mcp render settings tools: one call writes every section once and pushes one undo step")
{
    RenderSettingsToolFixture fixture;
    FakeRenderEngine& engine = fixture.engine;
    // Live-only state the write must keep: weather's fog, the runtime-driven effects, SSR and the latency features.
    engine.live.volumetricFog.uniformDensity = 0.3f;
    engine.live.underwater.enabled = true;
    engine.live.rainDroplets.intensity = 0.7f;
    engine.live.ssr.enabled = true;
    engine.live.frameGen.numFramesToGenerate = 2;
    engine.live.reflex.enabled = true;
    engine.scene.shadows.clipmapLevelCount = 6;
    engine.scene.display.presentMode = types::PresentMode::Fifo;

    const nlohmann::json result = fixture.callOk("render_settings_set", {
        {"postProcess", {{"enabled", true}, {"toneMapping", {{"enabled", true}, {"exposure", 1.8}}},
                         {"bloom", {{"enabled", true}, {"intensity", 1.5}, {"threshold", 0.6}}}}},
        {"shadows", {{"quality", "ultra"}}},
        {"display", {{"msaa", "X4"}}}
    });

    checkLog(engine.log, {"SetRenderSettings", "ApplyPostProcessSettings", "ApplyShadowSettings", "ApplyDisplaySettings",
                          "RenderSettingsChanged:PostProcess", "RenderSettingsChanged:Shadows",
                          "RenderSettingsChanged:Display", "PushUndo"});
    CHECK(result["changed"] == true);
    CHECK(result["diff"]["postProcess"]["enabled"] == true);
    CHECK(result["diff"]["postProcess"]["toneMapping"] == nlohmann::json{{"enabled", true}, {"exposure", 1.8}});
    CHECK(result["diff"]["postProcess"]["bloom"] == nlohmann::json{{"enabled", true}, {"intensity", 1.5}, {"threshold", 0.6}});
    CHECK(result["diff"]["shadows"] == nlohmann::json{{"quality", "Ultra"}}); // canonical spelling
    CHECK(result["diff"]["display"] == nlohmann::json{{"msaa", "X4"}});
    CHECK(result["effective"]["rendered"]["bloom"] == true);
    CHECK(result["effective"]["exposure"]["source"] == "manual");
    CHECK(result["effective"]["antiAliasing"] == "MSAA X4");

    // Both copies changed; everything the tools do not own kept its value.
    CHECK(engine.scene.postProcess.enabled);
    CHECK(engine.scene.postProcess.toneMapping.exposure == 1.8f);
    CHECK(engine.scene.postProcess.bloom.intensity == 1.5f);
    CHECK(engine.scene.shadows.quality == types::ShadowQuality::Ultra);
    CHECK(engine.scene.shadows.clipmapLevelCount == 6u);
    CHECK(engine.scene.display.msaa == types::MsaaSamples::X4);
    CHECK(engine.scene.display.presentMode == types::PresentMode::Fifo);
    CHECK(engine.live.enabled);
    CHECK(engine.live.bloom.threshold == 0.6f);
    CHECK(engine.live.volumetricFog.uniformDensity == 0.3f);
    CHECK(engine.live.underwater.enabled);
    CHECK(engine.live.rainDroplets.intensity == 0.7f);
    CHECK(engine.live.ssr.enabled);
    CHECK(engine.live.frameGen.numFramesToGenerate == 2u);
    CHECK(engine.live.reflex.enabled);
    REQUIRE(engine.appliedShadows.has_value());
    CHECK(engine.appliedShadows->shadows.quality == types::ShadowQuality::Ultra);
    CHECK(engine.appliedShadows->shadows.clipmapLevelCount == 6u);
    REQUIRE(engine.appliedDisplay.has_value());
    CHECK(engine.appliedDisplay->msaa == types::MsaaSamples::X4);
    CHECK(engine.appliedDisplay->presentMode == types::PresentMode::Fifo);

    REQUIRE(engine.undoStack.size() == 1);
    CHECK(engine.undoStack[0]->getDescription() == "MCP: Set render settings");
    CHECK(batchSize(engine.undoStack[0]) == 1); // one RenderSettingsSectionUndo holding the five sections
}

TEST_CASE("mcp render settings tools: undo restores only what the call changed, from fresh settings")
{
    RenderSettingsToolFixture fixture;
    FakeRenderEngine& engine = fixture.engine;
    fixture.callOk("render_settings_set", {{"postProcess", {{"bloom", {{"enabled", true}, {"intensity", 2}}}}}});
    REQUIRE(engine.undoStack.size() == 1);

    // Another edit (the editor's window) after the agent's.
    engine.scene.postProcess.vignette.enabled = true;
    engine.live.vignette.enabled = true;
    engine.log.clear();

    engine.undoStack[0]->undo();
    checkLog(engine.log, {"SetRenderSettings", "ApplyPostProcessSettings", "RenderSettingsChanged:PostProcess"});
    CHECK_FALSE(engine.scene.postProcess.bloom.enabled);
    CHECK(engine.scene.postProcess.bloom.intensity == 0.5f);
    CHECK_FALSE(engine.live.bloom.enabled);
    CHECK(engine.scene.postProcess.vignette.enabled); // the later edit survives
    CHECK(engine.live.vignette.enabled);

    engine.undoStack[0]->execute();
    CHECK(engine.scene.postProcess.bloom.enabled);
    CHECK(engine.scene.postProcess.bloom.intensity == 2.0f);
    CHECK(engine.scene.postProcess.vignette.enabled);
}

TEST_CASE("mcp render settings tools: VK-1661 FXAA is written to both copies and undone in one step")
{
    RenderSettingsToolFixture fixture;
    FakeRenderEngine& engine = fixture.engine;
    const nlohmann::json result = fixture.callOk("render_settings_set", {
        {"postProcess", {{"fxaa", {{"enabled", true}, {"quality", "ultra"}, {"subpixel", 0.5}}}}}});
    CHECK(result["diff"]["postProcess"]["fxaa"] ==
          nlohmann::json{{"enabled", true}, {"quality", "Ultra"}, {"subpixel", 0.5}}); // canonical spelling
    CHECK(engine.scene.postProcess.fxaa.enabled);
    CHECK(engine.scene.postProcess.fxaa.quality == ::postprocess::FXAAQuality::Ultra);
    CHECK(engine.scene.postProcess.fxaa.subpixel == 0.5f);
    CHECK(engine.scene.postProcess.fxaa.edgeThreshold == 0.125f); // keys the call did not name keep their value
    CHECK(engine.live.fxaa.enabled);
    CHECK(engine.live.fxaa.quality == ::postprocess::FXAAQuality::Ultra);
    CHECK(engine.live.fxaa.subpixel == 0.5f);

    REQUIRE(engine.undoStack.size() == 1);
    engine.undoStack[0]->undo();
    CHECK_FALSE(engine.scene.postProcess.fxaa.enabled);
    CHECK(engine.scene.postProcess.fxaa.quality == ::postprocess::FXAAQuality::Medium);
    CHECK(engine.scene.postProcess.fxaa.subpixel == 0.75f);
    CHECK_FALSE(engine.live.fxaa.enabled);
    CHECK(engine.live.fxaa.quality == ::postprocess::FXAAQuality::Medium);

    engine.undoStack[0]->execute();
    CHECK(engine.scene.postProcess.fxaa.enabled);
    CHECK(engine.live.fxaa.quality == ::postprocess::FXAAQuality::Ultra);
}

TEST_CASE("mcp render settings tools: undo and redo of a call that changed several sections")
{
    RenderSettingsToolFixture fixture;
    FakeRenderEngine& engine = fixture.engine;
    fixture.callOk("render_settings_set", {
        {"postProcess", {{"bloom", {{"enabled", true}}}}},
        {"shadows", {{"quality", "Low"}}},
        {"display", {{"msaa", "X2"}}}
    });
    REQUIRE(engine.undoStack.size() == 1);
    engine.log.clear();

    // One step re-writes every changed section once, through the same single store.
    engine.undoStack[0]->undo();
    checkLog(engine.log, {"SetRenderSettings", "ApplyPostProcessSettings", "ApplyShadowSettings", "ApplyDisplaySettings",
                          "RenderSettingsChanged:PostProcess", "RenderSettingsChanged:Shadows",
                          "RenderSettingsChanged:Display"});
    CHECK_FALSE(engine.scene.postProcess.bloom.enabled);
    CHECK_FALSE(engine.live.bloom.enabled);
    CHECK(engine.scene.shadows.quality == types::ShadowQuality::High);
    REQUIRE(engine.appliedShadows.has_value());
    CHECK(engine.appliedShadows->shadows.quality == types::ShadowQuality::High);
    CHECK(engine.scene.display.msaa == types::MsaaSamples::Off);
    REQUIRE(engine.appliedDisplay.has_value());
    CHECK(engine.appliedDisplay->msaa == types::MsaaSamples::Off);

    engine.undoStack[0]->execute();
    CHECK(engine.scene.postProcess.bloom.enabled);
    CHECK(engine.live.bloom.enabled);
    CHECK(engine.scene.shadows.quality == types::ShadowQuality::Low);
    CHECK(engine.scene.display.msaa == types::MsaaSamples::X2);
    CHECK(engine.appliedDisplay->msaa == types::MsaaSamples::X2);
}

TEST_CASE("mcp render settings tools: a no-op and an echoed render_settings_get change nothing")
{
    RenderSettingsToolFixture fixture;
    FakeRenderEngine& engine = fixture.engine;
    fixture.callOk("render_settings_set", {{"postProcess", {{"bloom", {{"threshold", 0.05}}}}}});
    engine.log.clear();
    engine.undoStack.clear();

    // 0.05 is reported as 0.05 and accepted back as exactly the stored float.
    const nlohmann::json got = fixture.callOk("render_settings_get", {{"includeRanges", false}});
    CHECK(got["postProcess"]["bloom"]["threshold"] == 0.05);
    CHECK_FALSE(got.contains("ranges"));

    const nlohmann::json echoed = fixture.callOk("render_settings_set", {
        {"postProcess", got["postProcess"]}, {"shadows", got["shadows"]}, {"display", got["display"]}});
    CHECK(echoed["changed"] == false);
    CHECK(echoed["diff"].empty());
    CHECK(engine.log.empty());
    CHECK(engine.undoStack.empty());

    // Enum names are matched case-insensitively and reported canonically.
    const nlohmann::json same = fixture.callOk("render_settings_set", {{"shadows", {{"quality", "high"}}}});
    CHECK(same["changed"] == false);
}

TEST_CASE("mcp render settings tools: changing the SSAO quality sets the sample count, like the editor's combo")
{
    RenderSettingsToolFixture fixture;
    ::postprocess::SSAOSettings& ssao = fixture.engine.scene.postProcess.ssao;
    REQUIRE(ssao.quality == ::postprocess::SSAOQuality::Medium);
    REQUIRE(ssao.kernelSize == 32);

    fixture.callOk("render_settings_set", {{"postProcess", {{"ssao", {{"quality", "High"}}}}}});
    CHECK(ssao.quality == ::postprocess::SSAOQuality::High);
    CHECK(ssao.kernelSize == 48);

    fixture.callOk("render_settings_set", {{"postProcess", {{"ssao", {{"quality", "Ultra"}, {"kernelSize", 40}}}}}});
    CHECK(ssao.kernelSize == 40); // an explicit kernelSize wins

    fixture.callOk("render_settings_set", {{"postProcess", {{"ssao", {{"quality", "Ultra"}}}}}});
    CHECK(ssao.kernelSize == 40); // the same quality again does not reset it

    // An echoed render_settings_get object repeats the current kernelSize: the quality change still applies.
    fixture.callOk("render_settings_set", {{"postProcess", {{"ssao", {{"quality", "Low"}, {"kernelSize", 40}}}}}});
    CHECK(ssao.quality == ::postprocess::SSAOQuality::Low);
    CHECK(ssao.kernelSize == 16);
}

TEST_CASE("mcp render settings tools: colour-grading LUT paths are project files, reported project-relative")
{
    RenderSettingsToolFixture fixture;
    FakeRenderEngine& engine = fixture.engine;
    touch(engine.assets / "luts" / "warm.cube");
    touch(engine.assets / "luts" / "warm.png");
    const std::string stored = fs::weakly_canonical(engine.assets / "luts" / "warm.cube").string();

    const nlohmann::json set = fixture.callOk("render_settings_set", {
        {"postProcess", {{"colorGrading", {{"enabled", true}, {"primaryLutPath", "luts/warm.cube"}}}}}});
    CHECK(engine.scene.postProcess.colorGrading.primaryLutPath == stored);
    CHECK(engine.live.colorGrading.primaryLutPath == stored);
    CHECK(set["diff"]["postProcess"]["colorGrading"]["primaryLutPath"] == "luts/warm.cube");

    const nlohmann::json got = fixture.callOk("render_settings_get", {{"includeRanges", false}});
    CHECK(got["postProcess"]["colorGrading"]["primaryLutPath"] == "luts/warm.cube");

    // The reported form and the stored form are both "the same file".
    CHECK(fixture.callOk("render_settings_set", {{"postProcess", {{"colorGrading", {{"primaryLutPath", "luts/warm.cube"}}}}}})
              ["changed"] == false);
    CHECK(fixture.callOk("render_settings_set", {{"postProcess", {{"colorGrading", {{"primaryLutPath", stored}}}}}})
              ["changed"] == false);

    CHECK(contains(fixture.argError("render_settings_set", {{"postProcess", {{"colorGrading", {{"primaryLutPath", "luts/cold.cube"}}}}}}),
                   "'postProcess.colorGrading.primaryLutPath': there is no .cube file at 'luts/cold.cube'"));
    CHECK(contains(fixture.argError("render_settings_set", {{"postProcess", {{"colorGrading", {{"primaryLutPath", "luts/warm.png"}}}}}}),
                   "must have the .cube extension"));
    CHECK(contains(fixture.argError("render_settings_set", {{"postProcess", {{"colorGrading", {{"secondaryLutPath", "../outside.cube"}}}}}}),
                   "is outside the allowed root"));
    CHECK(contains(fixture.argError("render_settings_set", {{"postProcess", {{"colorGrading", {{"primaryLutPath", 3}}}}}}),
                   "'postProcess.colorGrading.primaryLutPath' must be a string (got 3)"));

    // "" clears the slot.
    fixture.callOk("render_settings_set", {{"postProcess", {{"colorGrading", {{"primaryLutPath", ""}}}}}});
    CHECK(engine.scene.postProcess.colorGrading.primaryLutPath.empty());

    // A path outside the project (the editor's Browse) is kept when echoed, and a missing one is reported.
    engine.scene.postProcess.colorGrading.secondaryLutPath = "Z:/elsewhere/look.cube";
    CHECK(fixture.callOk("render_settings_set", {{"postProcess", {{"colorGrading", {{"secondaryLutPath", "Z:/elsewhere/look.cube"}}}}}})
              ["changed"] == false);
    const nlohmann::json warned = fixture.callOk("render_settings_get", {{"includeRanges", false}});
    CHECK(anyContains(warned["warnings"], "The LUT file 'Z:/elsewhere/look.cube' does not exist"));
}

TEST_CASE("mcp render settings tools: the edit tools refuse in Play mode and during a scene load")
{
    RenderSettingsToolFixture fixture;
    fixture.addPluginEffects();

    SUBCASE("Play mode")
    {
        fixture.engine.playMode = true;
        const mcp::ToolResult set = fixture.call("render_settings_set", {{"postProcess", {{"enabled", true}}}});
        CHECK(set.isError);
        CHECK(contains(set.text, "Cannot change the render settings in Play mode; call play_stop first."));
        const mcp::ToolResult effect = fixture.call("postprocess_effect_set", {{"id", 1}, {"enabled", false}});
        CHECK(effect.isError);
        CHECK(contains(effect.text, "Cannot toggle a plugin post-process effect in Play mode"));
        // The read tools still work.
        fixture.callOk("render_settings_get", nlohmann::json::object());
        fixture.callOk("postprocess_effect_list", nlohmann::json::object());
    }

    SUBCASE("a pending scene load")
    {
        fixture.engine.loadPending = true;
        const mcp::ToolResult set = fixture.call("render_settings_set", {{"postProcess", {{"enabled", true}}}});
        CHECK(set.isError);
        CHECK(contains(set.text, "while a scene load is in progress"));
    }

    CHECK(fixture.engine.log.empty());
    CHECK(fixture.engine.undoStack.empty());
}

TEST_CASE("mcp render settings tools: warnings explain why a change does not show")
{
    RenderSettingsToolFixture fixture;
    FakeRenderEngine& engine = fixture.engine;

    SUBCASE("the master switch is off")
    {
        const nlohmann::json got = fixture.callOk("render_settings_get", nlohmann::json::object());
        CHECK(anyContains(got["warnings"], "postProcess.enabled is false"));
        const nlohmann::json set = fixture.callOk("render_settings_set", {{"postProcess", {{"bloom", {{"enabled", true}}}}}});
        CHECK(anyContains(set["warnings"], "postProcess.enabled is false"));
        CHECK(set["effective"]["rendered"]["bloom"] == false);
    }

    SUBCASE("exposure with tone mapping off, and with auto exposure on")
    {
        engine.scene.postProcess.enabled = true;
        engine.live.enabled = true;
        const nlohmann::json off = fixture.callOk("render_settings_set", {{"postProcess", {{"toneMapping", {{"exposure", 2}}}}}});
        CHECK(anyContains(off["warnings"], "toneMapping.enabled is false"));
        CHECK(off["effective"]["exposure"]["source"] == "none");

        const nlohmann::json automatic = fixture.callOk("render_settings_set", {
            {"postProcess", {{"toneMapping", {{"enabled", true}, {"exposure", 3}}}, {"autoExposure", {{"enabled", true}}}}}});
        CHECK(anyContains(automatic["warnings"], "autoExposure.enabled overrides toneMapping.exposure"));
        CHECK(automatic["effective"]["exposure"]["source"] == "auto");
    }

    SUBCASE("DLSS where it is not supported, and MSAA under the upscaler")
    {
        engine.dlssSupported = false;
        const nlohmann::json unsupported = fixture.callOk("render_settings_set", {{"postProcess", {{"upscale", {{"enabled", true}}}}}});
        CHECK(anyContains(unsupported["warnings"], "DLSS is not supported on this GPU"));
        CHECK(unsupported["effective"]["upscaler"]["active"] == false);

        engine.dlssSupported = true;
        const nlohmann::json msaa = fixture.callOk("render_settings_set", {{"display", {{"msaa", "X8"}}}});
        CHECK(anyContains(msaa["warnings"], "display.msaa is saved but ignored while the upscaler is active"));
        CHECK(msaa["effective"]["antiAliasing"] == "DLSS");

        const nlohmann::json dlaa = fixture.callOk("render_settings_set", {{"postProcess", {{"upscale", {{"quality", "Native"}}}}}});
        CHECK(dlaa["effective"]["antiAliasing"] == "DLAA");
    }

    SUBCASE("VK-1661: FXAA alone, with MSAA and after the upscaler")
    {
        const nlohmann::json fxaa = fixture.callOk("render_settings_set", {
            {"postProcess", {{"enabled", true}, {"fxaa", {{"enabled", true}}}}}});
        CHECK(fxaa["effective"]["rendered"]["fxaa"] == true);
        CHECK(fxaa["effective"]["antiAliasing"] == "FXAA");
        CHECK_FALSE(anyContains(fxaa["warnings"], "postProcess.fxaa runs after DLSS"));

        const nlohmann::json msaa = fixture.callOk("render_settings_set", {{"display", {{"msaa", "X4"}}}});
        CHECK(msaa["effective"]["antiAliasing"] == "MSAA X4 + FXAA");

        // FXAA still runs after the upscaler (MSAA does not): it is reported and warned about.
        const nlohmann::json dlss = fixture.callOk("render_settings_set", {{"postProcess", {{"upscale", {{"enabled", true}}}}}});
        CHECK(dlss["effective"]["antiAliasing"] == "DLSS + FXAA");
        CHECK(anyContains(dlss["warnings"], "postProcess.fxaa runs after DLSS / DLAA"));

        const nlohmann::json dlaa = fixture.callOk("render_settings_set", {{"postProcess", {{"upscale", {{"quality", "Native"}}}}}});
        CHECK(dlaa["effective"]["antiAliasing"] == "DLAA + FXAA");
        const nlohmann::json got = fixture.callOk("render_settings_get", nlohmann::json::object());
        CHECK(anyContains(got["warnings"], "postProcess.fxaa runs after DLSS / DLAA"));
    }

    SUBCASE("VK-1661: the master switch turns on FXAA over an upscaler that already runs")
    {
        // The upscaler runs with the master switch off; FXAA (a built-in effect) starts with it.
        engine.scene.postProcess.fxaa.enabled = true;
        engine.live.fxaa.enabled = true;
        engine.scene.postProcess.upscale.enabled = true;
        engine.live.upscale.enabled = true;
        const nlohmann::json set = fixture.callOk("render_settings_set", {{"postProcess", {{"enabled", true}}}});
        CHECK(set["effective"]["antiAliasing"] == "DLSS + FXAA");
        CHECK(anyContains(set["warnings"], "postProcess.fxaa runs after DLSS / DLAA"));
    }

    SUBCASE("shadow quality Off")
    {
        const nlohmann::json set = fixture.callOk("render_settings_set", {{"shadows", {{"quality", "Off"}}}});
        CHECK(set["effective"]["shadowsRendered"] == false);
    }

    SUBCASE("the live settings differ from the saved ones")
    {
        engine.live.bloom.intensity = 3.0f;
        const nlohmann::json got = fixture.callOk("render_settings_get", nlohmann::json::object());
        CHECK(got["live"]["postProcess"]["bloom"] == nlohmann::json{{"intensity", 3.0}});
        CHECK(anyContains(got["warnings"], "differ from the saved ones"));
    }
}

TEST_CASE("mcp render settings tools: a failed store reports an error and pushes no undo")
{
    RenderSettingsToolFixture fixture;
    fixture.engine.storeSucceeds = false;
    const mcp::ToolResult result = fixture.call("render_settings_set", {{"postProcess", {{"enabled", true}}}});
    CHECK(result.isError);
    CHECK(contains(result.text, "The render settings were not changed (rolled back)"));
    CHECK(contains(result.text, "no scene is open"));
    CHECK(fixture.engine.undoStack.empty());
    CHECK_FALSE(fixture.engine.live.enabled);
}

TEST_CASE("mcp render settings tools: render_settings_get reports the settings, ranges and notes")
{
    RenderSettingsToolFixture fixture;
    const nlohmann::json got = fixture.callOk("render_settings_get", nlohmann::json::object());
    CHECK(got["postProcess"]["enabled"] == false);
    CHECK(got["postProcess"]["toneMapping"]["mode"] == "ACES");
    CHECK(got["postProcess"]["ssao"]["quality"] == "Medium");
    CHECK(got["postProcess"]["fxaa"]["quality"] == "Medium");
    CHECK(got["shadows"]["quality"] == "High");
    CHECK(got["display"]["msaa"] == "Off");
    CHECK_FALSE(got["postProcess"].contains("volumetricFog"));
    CHECK(got["ranges"]["postProcess"]["bloom"]["intensity"] == nlohmann::json{{"type", "number"}, {"min", 0.0}, {"max", 5.0}});
    CHECK(got["ranges"]["postProcess"]["colorGrading"]["primaryLutPath"] ==
          nlohmann::json{{"type", "string"}, {"extension", ".cube"}});
    CHECK(got["ranges"]["display"]["msaa"]["values"] == nlohmann::json::array({"Off", "X2", "X4", "X8"}));
    CHECK(got["effective"]["upscaler"]["requested"] == false);
    CHECK(got["effective"]["antiAliasing"] == "none");
    CHECK(got["effective"]["rendered"]["fxaa"] == false);
    CHECK(got["ranges"]["postProcess"]["fxaa"]["edgeThreshold"] ==
          nlohmann::json{{"type", "number"}, {"min", 0.063}, {"max", 0.333}});
    CHECK(got["ranges"]["postProcess"]["fxaa"]["quality"]["values"] ==
          nlohmann::json::array({"Low", "Medium", "High", "Ultra"}));
    // VK-1661: FXAA is an anti-aliasing option again.
    bool mentionsFxaa = false;
    bool deniesFxaa = false;
    for (const nlohmann::json& note : got["notes"])
    {
        mentionsFxaa = mentionsFxaa || contains(note.get<std::string>(), "postProcess.fxaa");
        deniesFxaa = deniesFxaa || contains(note.get<std::string>(), "There is no FXAA");
    }
    CHECK(mentionsFxaa);
    CHECK_FALSE(deniesFxaa);
}

TEST_CASE("mcp render settings tools: plugin post-process effects are listed and toggled by id or name")
{
    RenderSettingsToolFixture fixture;
    FakeRenderEngine& engine = fixture.engine;
    fixture.addPluginEffects();

    const nlohmann::json listed = fixture.callOk("postprocess_effect_list", nlohmann::json::object());
    REQUIRE(listed["effects"].size() == 4);
    CHECK(listed["effects"][0] == nlohmann::json{{"id", 1}, {"plugin", "PostProcessFX"}, {"name", "PPFX_Exposure"},
                                                 {"priority", 95}, {"stage", "beforeTonemap"}, {"enabled", true},
                                                 {"pluginActive", true}, {"rendering", true}});
    CHECK(listed["effects"][1]["stage"] == "afterTonemap");

    SUBCASE("by id, with undo and redo")
    {
        const nlohmann::json off = fixture.callOk("postprocess_effect_set", {{"id", 2}, {"enabled", false}});
        CHECK(off["changed"] == true);
        CHECK(off["effect"]["enabled"] == false);
        checkLog(engine.log, {"SetPluginEffect:2=false", "PushUndo"});
        REQUIRE(engine.undoStack.size() == 1);
        CHECK(engine.undoStack[0]->getDescription() == "MCP: Disable PostProcessFX/PPFX_Grade");

        engine.undoStack[0]->undo();
        CHECK(engine.effects[1].requestedEnabled);
        engine.undoStack[0]->execute();
        CHECK_FALSE(engine.effects[1].requestedEnabled);
    }

    SUBCASE("by name: exact, then a unique case-insensitive match, plugin to disambiguate")
    {
        fixture.callOk("postprocess_effect_set", {{"name", "ppfx_exposure"}, {"enabled", false}});
        CHECK_FALSE(engine.effects[0].requestedEnabled);
        fixture.callOk("postprocess_effect_set", {{"plugin", "OtherFX"}, {"name", "PPFX_Grade"}, {"enabled", true}});
        CHECK(engine.effects[2].requestedEnabled);
        CHECK(engine.effects[1].requestedEnabled); // the other PPFX_Grade is untouched
    }

    SUBCASE("ambiguous, empty and unknown names, and id plus name")
    {
        CHECK(contains(fixture.argError("postprocess_effect_set", {{"name", "PPFX_Grade"}, {"enabled", false}}),
                       "'PPFX_Grade' names 2 effects; pass 'id' (or 'plugin'): PostProcessFX/PPFX_Grade (id 2), "
                       "OtherFX/PPFX_Grade (id 3)"));
        CHECK(contains(fixture.argError("postprocess_effect_set", {{"name", ""}, {"enabled", false}}), "'name' is empty"));
        CHECK(contains(fixture.argError("postprocess_effect_set", {{"name", "Sharpen"}, {"enabled", false}}),
                       "no plugin post-process effect is named 'Sharpen'"));
        CHECK(contains(fixture.argError("postprocess_effect_set", {{"id", 9}, {"enabled", false}}),
                       "no plugin post-process effect has id 9; effects: PostProcessFX/PPFX_Exposure (id 1)"));
        CHECK(contains(fixture.argError("postprocess_effect_set", {{"id", 1}, {"name", "PPFX_Exposure"}, {"enabled", false}}),
                       "pass either 'id' or 'plugin' + 'name', not both"));
        CHECK(contains(fixture.argError("postprocess_effect_set", {{"id", 1}}), "missing required argument 'enabled'"));
        CHECK(contains(fixture.argError("postprocess_effect_set", {{"id", 4}, {"enabled", false}}), "<no error>"));
        CHECK(engine.effects[3].requestedEnabled == false); // the nameless effect is reachable by id
    }

    SUBCASE("already in that state: no undo; a deactivated plugin keeps the request")
    {
        const nlohmann::json same = fixture.callOk("postprocess_effect_set", {{"id", 1}, {"enabled", true}});
        CHECK(same["changed"] == false);
        CHECK(engine.undoStack.empty());

        engine.effects[0].pluginActive = false;
        const nlohmann::json inactive = fixture.callOk("postprocess_effect_set", {{"id", 1}, {"enabled", false}});
        CHECK(inactive["effect"]["rendering"] == false);
        CHECK(anyContains(inactive["warnings"], "is deactivated for this scene"));
    }
}

TEST_CASE("mcp render settings tools: a plugin effect that vanished mid-call reports an error and pushes no undo")
{
    RenderSettingsToolFixture fixture;
    FakeRenderEngine& engine = fixture.engine;
    fixture.addPluginEffects();
    engine.effectSetSucceeds = false;

    const mcp::ToolResult result = fixture.call("postprocess_effect_set", {{"id", 2}, {"enabled", false}});
    CHECK(result.isError);
    CHECK(contains(result.text, "The effect was not changed (rolled back): the effect PostProcessFX/PPFX_Grade no "
                                "longer exists"));
    // The tool's own set, then the rollback's attempt to restore; nothing reaches the undo stack.
    checkLog(engine.log, {"SetPluginEffect:2=false", "SetPluginEffect:2=true"});
    CHECK(engine.undoStack.empty());
    CHECK(engine.effects[1].requestedEnabled);
}

TEST_CASE("mcp render settings tools: plugin effects without a plugin runtime")
{
    RenderSettingsToolFixture fixture;
    events::EventDispatcher::instance().unregisterQueryHandler<events::editor::ListPluginPostProcessEffectsQuery>();
    std::string message = "<no error>";
    try
    {
        fixture.call("postprocess_effect_list", nlohmann::json::object());
    }
    catch (const mcp::ArgError& e)
    {
        message = std::string("<an ArgError: ") + e.what() + ">";
    }
    catch (const std::runtime_error& e)
    {
        message = e.what(); // reported to the agent as an isError result
    }
    CHECK(contains(message, "Plugin post-process effects are unavailable"));
}

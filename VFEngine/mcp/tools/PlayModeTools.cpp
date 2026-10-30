#include "CoreTools.hpp"
#include "TerrainToolSupport.hpp"
#include "../protocol/ArgReader.hpp"

#include "events/EventDispatcher.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/navmesh/NavmeshAuthoringEvents.hpp"
#include "events/scripting/ScriptingEvents.hpp"
#include "events/volumetric/VolumetricNavEvents.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace mcp::tools
{
    namespace
    {
        constexpr int64_t maxStepFrames = 60;

        const std::vector<std::string> unsavedTerrainModes{"refuse", "save", "discard"};

        // VK-1653. play_stop reloads every terrain from its .vfTerrain (ScenePersistenceService), so terrain
        // edits that are not on disk are lost then, and a never-saved terrain comes back as an empty shell.
        // Returns why Play must not start, or nullopt; `saved` and `warnings` report what was done.
        std::optional<std::string> settleUnsavedTerrain(const std::string& mode, nlohmann::json& saved,
                                                        std::vector<std::string>& warnings)
        {
            const std::optional<std::vector<services::TerrainSummary>> terrains = tryListTerrains();
            if (!terrains.has_value() || isWorldModeActive())
            {
                return std::nullopt; // no terrain service, or World mode (its sectors own the terrain)
            }

            std::string pending;
            for (const services::TerrainSummary& terrain : *terrains)
            {
                const std::string reason = terrainSaveReason(terrain);
                if (!reason.empty())
                {
                    pending += (pending.empty() ? "" : ", ") + describeTerrain(terrain) + " (" + reason + ")";
                }
            }
            if (pending.empty())
            {
                return std::nullopt;
            }

            if (mode == "refuse")
            {
                return "Play NOT started: terrain " + pending + " is not saved, and play_stop reloads terrain from its "
                       ".vfTerrain, so those edits would be lost. Call play_start with unsavedTerrain:\"save\" (or "
                       "scene_save / terrain_save first), or unsavedTerrain:\"discard\" to accept losing them";
            }
            if (mode == "save")
            {
                TerrainSaveReport report = saveUnsavedTerrains();
                if (!report.ok)
                {
                    return "Play NOT started: " + report.error;
                }
                saved = std::move(report.saved);
                warnings.insert(warnings.end(), report.warnings.begin(), report.warnings.end());
                return std::nullopt;
            }
            warnings.push_back("Terrain " + pending + " will revert to its saved file when Play stops (a never-saved "
                               "terrain comes back as an empty shell)");
            return std::nullopt;
        }

        // VK-1660: a navmesh or volume bake that never reached a file still drives Play. play_stop (finishLoad)
        // reloads the scene's saved navmesh in its place when the scene has one; a volume keeps its grid; a scene
        // change drops both. A missing service (tests) or a failing query adds no warning.
        void addUnsavedNavigationWarnings(std::vector<std::string>& warnings)
        {
            auto& dispatcher = events::EventDispatcher::instance();
            try
            {
                const events::navmesh::NavmeshStatus navmesh =
                    dispatcher.query(events::navmesh::GetNavmeshStatusQuery{});
                if (navmesh.unsavedChanges)
                {
                    warnings.push_back(navmesh.indexPath.empty()
                        ? std::string("The navmesh has a bake that was never saved: it survives play_stop, but the "
                                      "scene file has no navmesh. After play_stop, run navmesh_bake (it saves what it "
                                      "bakes), then scene_save")
                        : "The navmesh has a bake that was never saved: play_stop reloads the scene's saved navmesh ('" +
                              navmesh.indexPath + "') in its place. To keep a new navmesh, run navmesh_bake after "
                              "play_stop (it saves what it bakes)");
                }
            }
            catch (const std::exception&)
            {
            }
            try
            {
                for (const services::VolumetricVolumeStatus& volume :
                     dispatcher.query(events::volumetric::ListVolumetricVolumesQuery{}))
                {
                    if (volume.baked && volume.file.empty())
                    {
                        warnings.push_back("Volume " + std::to_string(static_cast<uint32_t>(volume.entity.id)) +
                                           " ('" + volume.name + "') has a bake that was never saved to a .vfVolNav: "
                                           "it survives play_stop, but the scene file loads the volume unbaked. After "
                                           "play_stop, run volumetric_bake on it (it saves what it bakes), then "
                                           "scene_save");
                    }
                }
            }
            catch (const std::exception&)
            {
            }
        }

        nlohmann::json playState()
        {
            auto& dispatcher = events::EventDispatcher::instance();
            const bool playing = dispatcher.query(events::editor::IsPlayModeQuery{});
            return {
                {"mode", playing ? "play" : "edit"},
                {"paused", dispatcher.query(events::editor::IsEditorPausedQuery{})},
                {"timeScale", dispatcher.query(events::editor::GetTimeScaleQuery{})}
            };
        }

        void registerPlayStart(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "play_start";
            tool.title = "Enter Play mode";
            tool.description =
                "Enter Play mode (runs the scene's scripts and physics). If scripts are not compiled they are "
                "built first; on build failure Play is NOT entered and the compile errors are returned. "
                "NOTE: after script_write, call scripts_build yourself — an already-compiled project is not "
                "rebuilt here. Use logs_read to watch script output and play_stop to return to Edit mode "
                "(the edit-time scene is restored). play_stop reloads terrain from its .vfTerrain file, so "
                "'unsavedTerrain' decides what happens to unsaved terrain edits: 'refuse' (default) does not start "
                "Play, 'save' saves them first (like scene_save does), 'discard' starts anyway and they are lost "
                "on play_stop. A navmesh or volumetric volume bake that was never saved only adds a warning (when the "
                "scene has a saved navmesh, play_stop reloads it in place of the bake). Returns {mode, paused, "
                "timeScale} (+ terrainsSaved, warnings).";
            tool.inputSchema = schema::object({
                {"withDebugger", schema::boolean("Also start the mType debug server for VS Code. Default false.")},
                {"unsavedTerrain", schema::enumString("Terrain with unsaved changes: refuse (default), save, discard.",
                                                      unsavedTerrainModes)}
            });
            tool.timeout = std::chrono::milliseconds(120000);
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                const std::string unsavedTerrain = reader.optEnum("unsavedTerrain", unsavedTerrainModes, "refuse");
                const bool withDebugger = reader.optBool("withDebugger", false);
                auto& dispatcher = events::EventDispatcher::instance();

                if (dispatcher.query(events::editor::IsPlayModeQuery{}))
                {
                    return ToolResult::ok(playState(), "Already in Play mode.");
                }

                nlohmann::json terrainsSaved = nlohmann::json::array();
                std::vector<std::string> warnings;
                if (const std::optional<std::string> refusal = settleUnsavedTerrain(unsavedTerrain, terrainsSaved, warnings))
                {
                    return ToolResult::error(*refusal);
                }
                addUnsavedNavigationWarnings(warnings);

                // SetEditorModeCommand builds on its own when needed but only logs the
                // errors; building here first lets the model see them.
                if (!dispatcher.query(events::scripting::IsScriptsCompiledQuery{}))
                {
                    services::ScriptBuildResult build = dispatcher.execute(events::scripting::BuildScriptsCommand{});
                    if (!build.success)
                    {
                        nlohmann::json out = playState();
                        out["build"] = {
                            {"success", false},
                            {"filesCompiled", build.filesCompiled},
                            {"filesFailed", build.filesFailed},
                            {"errors", build.errors}
                        };
                        std::string text = "Play NOT started: script build failed.";
                        for (const std::string& error : build.errors)
                        {
                            text += "\n- " + error;
                        }
                        ToolResult result = ToolResult::ok(std::move(out), std::move(text));
                        result.isError = true;
                        return result;
                    }
                }

                events::editor::SetEditorModeCommand command;
                command.mode = services::EditorMode::Play;
                command.withDebugger = withDebugger;
                dispatcher.execute(command);

                nlohmann::json state = playState();
                if (!terrainsSaved.empty())
                {
                    state["terrainsSaved"] = std::move(terrainsSaved);
                }
                if (!warnings.empty())
                {
                    state["warnings"] = warnings;
                }
                if (state["mode"] != "play")
                {
                    ToolResult result = ToolResult::ok(std::move(state),
                                                       "Play mode was not entered; check logs_read for the reason.");
                    result.isError = true;
                    return result;
                }
                return ToolResult::ok(std::move(state));
            };
            registry.add(std::move(tool));
        }

        void registerPlayStop(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "play_stop";
            tool.title = "Exit Play mode";
            tool.description =
                "Leave Play mode and return to Edit mode. The edit-time scene is reloaded asynchronously, so "
                "entity ids from before play_start may change; re-query with scene_get_hierarchy. "
                "Returns {mode, paused, timeScale}.";
            tool.timeout = std::chrono::milliseconds(30000);
            tool.handler = [](const nlohmann::json&) -> ToolResult
            {
                auto& dispatcher = events::EventDispatcher::instance();
                if (!dispatcher.query(events::editor::IsPlayModeQuery{}))
                {
                    return ToolResult::ok(playState(), "Already in Edit mode.");
                }

                events::editor::SetEditorModeCommand command;
                command.mode = services::EditorMode::Edit;
                dispatcher.execute(command);
                return ToolResult::ok(playState());
            };
            registry.add(std::move(tool));
        }

        void registerPlayPause(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "play_pause";
            tool.title = "Pause / resume Play mode";
            tool.description =
                "Pause (paused=true) or resume (paused=false) gameplay time while in Play mode. While paused, "
                "play_step advances single frames. Returns {mode, paused, timeScale}.";
            tool.inputSchema = schema::object({
                {"paused", schema::boolean("true to pause, false to resume")}
            }, {"paused"});
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                const bool paused = reader.requireBool("paused");
                auto& dispatcher = events::EventDispatcher::instance();
                if (!dispatcher.query(events::editor::IsPlayModeQuery{}))
                {
                    return ToolResult::error("Not in Play mode; call play_start first");
                }

                events::editor::SetEditorPausedCommand command;
                command.paused = paused;
                dispatcher.execute(command);
                return ToolResult::ok(playState());
            };
            registry.add(std::move(tool));
        }

        void registerPlayStep(ToolRegistry& registry, const ToolContext& context)
        {
            ToolDef tool;
            tool.name = "play_step";
            tool.title = "Step frames";
            tool.description =
                "Advance gameplay by 'frames' frames (default 1, max 60) while Play mode is paused "
                "(play_pause paused=true first). Each step is consumed by the next editor frame. "
                "Returns {mode, paused, timeScale, stepped}.";
            tool.inputSchema = schema::object({
                {"frames", schema::integer("Frames to advance (1-60). Default 1.")}
            });
            // Worker: one step request per editor frame. The engine's step flag is a
            // bool, so two requests inside the same frame would collapse into one.
            tool.affinity = ThreadAffinity::Worker;
            tool.handler = [context](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                const int64_t frames = reader.optInt("frames", 1);
                if (frames < 1 || frames > maxStepFrames)
                {
                    throw ArgError("argument 'frames' must be between 1 and 60");
                }

                int64_t stepped = 0;
                for (int64_t i = 0; i < frames; ++i)
                {
                    nlohmann::json outcome = context.runOnMain([]() -> nlohmann::json
                    {
                        auto& dispatcher = events::EventDispatcher::instance();
                        if (!dispatcher.query(events::editor::IsPlayModeQuery{}) ||
                            !dispatcher.query(events::editor::IsEditorPausedQuery{}))
                        {
                            return false;
                        }
                        dispatcher.execute(events::editor::StepFrameCommand{});
                        return true;
                    });
                    if (!outcome.get<bool>())
                    {
                        if (stepped == 0)
                        {
                            return ToolResult::error("play_step requires Play mode and paused "
                                                     "(play_start, then play_pause paused=true)");
                        }
                        break;
                    }
                    ++stepped;

                    // MainThreadQueue::drain keeps running tasks queued while it is still
                    // draining; let this frame's drain finish so the next request lands in
                    // the following frame, after the timer has consumed this one.
                    if (i + 1 < frames)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                }

                nlohmann::json state = context.runOnMain([]() -> nlohmann::json { return playState(); });
                state["stepped"] = stepped;
                return ToolResult::ok(std::move(state));
            };
            registry.add(std::move(tool));
        }
    }

    void registerPlayModeTools(ToolRegistry& registry, const ToolContext& context)
    {
        registerPlayStart(registry);
        registerPlayStop(registry);
        registerPlayPause(registry);
        registerPlayStep(registry, context);
    }
}

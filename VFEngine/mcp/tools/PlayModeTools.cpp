#include "CoreTools.hpp"
#include "../protocol/ArgReader.hpp"

#include "events/EventDispatcher.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/scripting/ScriptingEvents.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

namespace mcp::tools
{
    namespace
    {
        constexpr int64_t maxStepFrames = 60;

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
                "(the edit-time scene is restored). Returns {mode, paused, timeScale}.";
            tool.inputSchema = schema::object({
                {"withDebugger", schema::boolean("Also start the mType debug server for VS Code. Default false.")}
            });
            tool.timeout = std::chrono::milliseconds(120000);
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                auto& dispatcher = events::EventDispatcher::instance();

                if (dispatcher.query(events::editor::IsPlayModeQuery{}))
                {
                    return ToolResult::ok(playState(), "Already in Play mode.");
                }

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
                command.withDebugger = reader.optBool("withDebugger", false);
                dispatcher.execute(command);

                nlohmann::json state = playState();
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

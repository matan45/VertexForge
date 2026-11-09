#include "CoreTools.hpp"
#include "../protocol/ArgReader.hpp"
#include "../protocol/McpServer.hpp"
#include "PathSandbox.hpp"

#include "events/EventDispatcher.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>

namespace mcp::tools
{
    namespace
    {
        // editor_get_info is a status probe: one unregistered handler should blank
        // its own field rather than fail the whole call.
        template <typename TQuery>
        std::optional<typename TQuery::ResultType> tryQuery(const TQuery& query)
        {
            try
            {
                return events::EventDispatcher::instance().query(query);
            }
            catch (const std::runtime_error&)
            {
                return std::nullopt;
            }
        }

        nlohmann::json projectToJson(const config::ProjectConfig& project)
        {
            nlohmann::json out{
                {"name", project.projectName},
                {"version", project.version},
                {"workingDirectory", project.workingDirectory},
                {"startupScene", project.startupScene},
                {"fontFallbackChain", project.fontFallbackChain},
                {"exeIconPath", project.exeIconPath},
                {"schemaVersion", project.schemaVersion.toString()}
            };
            out["inputMapping"] = project.inputMapping.has_value() ? nlohmann::json(*project.inputMapping) : nlohmann::json(nullptr);
            out["engineVersion"] = project.engineVersion.has_value() ? nlohmann::json(*project.engineVersion) : nlohmann::json(nullptr);
            out["lastModified"] = project.lastModified.has_value() ? nlohmann::json(*project.lastModified) : nlohmann::json(nullptr);
            out["pluginApiVersion"] = project.pluginApiVersion.has_value() ? nlohmann::json(*project.pluginApiVersion) : nlohmann::json(nullptr);
            return out;
        }

        void registerEditorGetInfo(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "editor_get_info";
            tool.title = "Editor status";
            tool.description =
                "Snapshot of the editor: loaded project (name, .vfproject path, working directory), current "
                "scene file path (empty = never saved/loaded), mode ('edit' | 'play'), paused flag, selected "
                "entity ids and MCP server info. Call this first to orient yourself.";
            tool.inputSchema = schema::object(nlohmann::json::object());
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json&) -> ToolResult
            {
                nlohmann::json info = nlohmann::json::object();

                nlohmann::json project = nullptr;
                if (tryQuery(events::project::IsProjectLoadedQuery{}).value_or(false))
                {
                    auto config = tryQuery(events::project::GetCurrentProjectQuery{});
                    auto path = tryQuery(events::project::GetProjectPathQuery{});
                    project = nlohmann::json::object();
                    if (config.has_value() && config->has_value())
                    {
                        project["name"] = (*config)->projectName;
                        project["workingDirectory"] = pathToUtf8(std::filesystem::path((*config)->workingDirectory));
                        project["startupScene"] = (*config)->startupScene;
                    }
                    project["path"] = path.has_value() && path->has_value() ? nlohmann::json(**path) : nlohmann::json(nullptr);
                }
                info["project"] = std::move(project);

                auto scenePath = tryQuery(events::scene::GetCurrentScenePathQuery{});
                info["scenePath"] = scenePath.has_value() ? nlohmann::json(*scenePath) : nlohmann::json(nullptr);

                auto mode = tryQuery(events::editor::GetEditorModeQuery{});
                if (mode.has_value())
                {
                    info["mode"] = *mode == services::EditorMode::Play ? "play" : "edit";
                }
                else
                {
                    info["mode"] = nullptr;
                }
                auto paused = tryQuery(events::editor::IsEditorPausedQuery{});
                info["paused"] = paused.has_value() ? nlohmann::json(*paused) : nlohmann::json(nullptr);

                nlohmann::json selection = nlohmann::json::array();
                if (auto selected = tryQuery(events::scene::GetSelectedEntitiesQuery{}); selected.has_value())
                {
                    for (const services::EntityHandle& handle : *selected)
                    {
                        if (handle.isValid())
                        {
                            selection.push_back(static_cast<uint32_t>(handle.id));
                        }
                    }
                }
                info["selection"] = std::move(selection);

                info["mcp"] = {
                    {"server", "vertexforge-editor"},
                    {"protocolVersion", McpServer::latestProtocolVersion},
                    {"conventions", "Entity ids are uint32; vectors are [x,y,z]; rotations are Euler degrees; "
                                    "relative asset/scene paths resolve against the project working directory."}
                };
                return ToolResult::ok(std::move(info));
            };
            registry.add(std::move(tool));
        }

        void registerProjectInfo(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "project_info";
            tool.title = "Project info";
            tool.description =
                "Full configuration of the loaded project: name, version, workingDirectory (asset root; "
                "relative paths in other tools resolve against it), startupScene, fontFallbackChain, "
                "inputMapping, engineVersion, pluginApiVersion, and the .vfproject path. Errors when no project is loaded.";
            tool.inputSchema = schema::object(nlohmann::json::object());
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json&) -> ToolResult
            {
                auto& dispatcher = events::EventDispatcher::instance();
                auto project = dispatcher.query(events::project::GetCurrentProjectQuery{});
                if (!project.has_value())
                {
                    return ToolResult::error("No project is loaded; use project_open");
                }

                nlohmann::json out = projectToJson(*project);
                auto path = dispatcher.query(events::project::GetProjectPathQuery{});
                out["path"] = path.has_value() ? nlohmann::json(*path) : nlohmann::json(nullptr);
                return ToolResult::ok(std::move(out));
            };
            registry.add(std::move(tool));
        }

        void registerProjectOpen(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "project_open";
            tool.title = "Open project";
            tool.description =
                "Load a .vfproject file and queue its startup scene (same steps as launching Editor.exe with a "
                "project path). The current scene is replaced and unsaved changes are lost. The scene load is "
                "deferred to the next frame(s); call scene_get_hierarchy afterwards. Switching projects inside "
                "a running editor is less exercised than a fresh launch - if anything looks stale, restart the "
                "editor with the project path instead. Not allowed in Play mode.";
            tool.inputSchema = schema::object({
                {"path", schema::string("Absolute path to the .vfproject file")}
            }, {"path"});
            tool.destructive = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                std::string path = reader.requireString("path");
                if (path.empty())
                {
                    throw ArgError("argument 'path' must not be empty");
                }

                auto& dispatcher = events::EventDispatcher::instance();
                if (dispatcher.query(events::editor::IsPlayModeQuery{}))
                {
                    return ToolResult::error("Cannot open a project in Play mode; call play_stop first");
                }

                // `path` is the agent's UTF-8; the engine takes narrow path strings.
                const std::filesystem::path projectPath = pathFromUtf8(path);
                std::error_code ec;
                if (!std::filesystem::is_regular_file(projectPath, ec))
                {
                    return ToolResult::error("Project file not found: " + path);
                }

                events::project::LoadProjectCommand loadCommand;
                loadCommand.filePath = projectPath.string();
                if (!dispatcher.execute(loadCommand))
                {
                    return ToolResult::error("Failed to load project '" + path + "' (see logs_read)");
                }

                nlohmann::json out{{"opened", true}, {"path", path}};

                // Mirrors EditorHandler::loadProject: queue the startup scene when it exists.
                auto project = dispatcher.query(events::project::GetCurrentProjectQuery{});
                out["startupSceneQueued"] = false;
                if (project.has_value())
                {
                    out["name"] = project->projectName;
                    out["workingDirectory"] = pathToUtf8(std::filesystem::path(project->workingDirectory));
                    if (!project->startupScene.empty())
                    {
                        std::filesystem::path scenePath =
                            std::filesystem::path(project->workingDirectory) / project->startupScene;
                        if (std::filesystem::exists(scenePath, ec))
                        {
                            events::scene::LoadSceneCommand sceneCommand;
                            sceneCommand.filePath = scenePath.string();
                            out["startupSceneQueued"] = dispatcher.execute(sceneCommand);
                            out["startupScene"] = pathToUtf8(scenePath);
                        }
                        else
                        {
                            out["warning"] = "Startup scene not found: " + pathToUtf8(scenePath);
                        }
                    }
                }
                return ToolResult::ok(std::move(out));
            };
            registry.add(std::move(tool));
        }
    }

    void registerEditorTools(ToolRegistry& registry, const ToolContext&)
    {
        registerEditorGetInfo(registry);
        registerProjectInfo(registry);
        registerProjectOpen(registry);
    }
}

#include "CoreTools.hpp"
#include "PathSandbox.hpp"
#include "../protocol/ArgReader.hpp"

#include "events/EventDispatcher.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/scripting/ScriptingEvents.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace mcp::tools
{
    namespace
    {
        namespace fs = std::filesystem;

        // mType sources are small; anything larger is almost certainly a mistake.
        constexpr std::uintmax_t maxScriptBytes = 1024 * 1024;

        // Main thread only. ProjectConfig::workingDirectory is the asset root (the
        // project's "Assets" folder); scripts live in <workingDirectory>/scripts with
        // the compiled sources under game/ (see scripts.mtproj: Include game/**/*.mt).
        fs::path scriptsRootOnMain()
        {
            auto project = events::EventDispatcher::instance().query(events::project::GetCurrentProjectQuery{});
            if (!project.has_value() || project->workingDirectory.empty())
            {
                throw std::runtime_error("No project is loaded; open one with project_open first");
            }
            // workingDirectory is built with path::string() (narrow) by ProjectServiceImpl.
            return fs::absolute(fs::path(project->workingDirectory)) / "scripts";
        }

        // Worker thread: hop to the main thread for the project query.
        fs::path scriptsRoot(const ToolContext& context)
        {
            nlohmann::json root = context.runOnMain([]() -> nlohmann::json
            {
                return pathToUtf8(scriptsRootOnMain());
            });
            return pathFromUtf8(root.get<std::string>());
        }

        std::string relativeToRoot(const fs::path& root, const fs::path& path)
        {
            std::error_code ec;
            fs::path relative = fs::relative(path, root, ec);
            return genericPathToUtf8(ec || relative.empty() ? path : relative);
        }

        // Resolves a scripts-root-relative path ("game/Player.mt") to a canonical .mt
        // path inside <scriptsRoot> (or inside <scriptsRoot>/game when gameOnly).
        fs::path resolveScript(const fs::path& root, const std::string& path, bool gameOnly)
        {
            std::string error;
            auto resolved = resolveInside(root, path, error, ".mt");
            if (!resolved)
            {
                throw ArgError(error);
            }
            if (gameOnly)
            {
                auto insideGame = resolveInside(root / "game", pathToUtf8(*resolved), error);
                if (!insideGame)
                {
                    throw ArgError("path '" + path + "' must be under game/ (only game/**/*.mt is compiled "
                                   "and attachable; lib/ is the read-only engine API)");
                }
                return *insideGame;
            }
            return *resolved;
        }

        // Script paths stored on a ScriptComponent are whatever the attaching caller
        // passed (the editor passes absolute file-dialog paths); relative ones are
        // resolved against the asset root like AssetRef::fromPath does.
        bool sameScriptPath(const fs::path& assetsRoot, const std::string& stored, const fs::path& target)
        {
            fs::path storedPath(stored);
            if (storedPath.is_relative())
            {
                storedPath = assetsRoot / storedPath;
            }
            std::error_code ec;
            const fs::path canonical = fs::weakly_canonical(storedPath, ec);
            if (ec)
            {
                return false;
            }
            return detail::isWithin(canonical, target) && detail::isWithin(target, canonical);
        }

        services::EntityHandle requireLiveEntity(const ArgReader& reader, const char* name)
        {
            uint32_t id = reader.requireEntity(name);
            services::EntityHandle handle;
            handle.id = static_cast<uint64_t>(id);

            events::scene::GetEntityQuery query;
            query.entity = handle;
            if (!events::EventDispatcher::instance().query(query).has_value())
            {
                throw std::runtime_error("Entity " + std::to_string(id) + " (argument '" + name +
                                         "') does not exist; use entity_find or scene_get_hierarchy");
            }
            return handle;
        }

        nlohmann::json attachedScripts(const services::EntityHandle& entity, const fs::path& scriptsDir)
        {
            events::scripting::GetScriptPathsQuery query;
            query.entity = entity;
            nlohmann::json out = nlohmann::json::array();
            for (const std::string& stored : events::EventDispatcher::instance().query(query))
            {
                fs::path storedPath(stored);
                if (storedPath.is_relative())
                {
                    storedPath = scriptsDir.parent_path() / storedPath;
                }
                std::error_code ec;
                const fs::path canonical = fs::weakly_canonical(storedPath, ec);
                out.push_back(ec ? stored : relativeToRoot(scriptsDir, canonical));
            }
            return out;
        }

        void registerScriptList(ToolRegistry& registry, const ToolContext& context)
        {
            ToolDef tool;
            tool.name = "script_list";
            tool.title = "List scripts";
            tool.description =
                "List mType (.mt) scripts. Paths are relative to the project scripts root "
                "(<asset root>/scripts): game/** holds the compiled, attachable game scripts; lib/** "
                "(includeLib=true) is the engine scripting API (lib/engine/*.mt: Log, Entity, Transform, "
                "Input, Physics...) — read those with script_read to learn the API. Returns "
                "{scriptsRoot, scripts:[{path, bytes}]}.";
            tool.inputSchema = schema::object({
                {"includeLib", schema::boolean("Also list lib/**/*.mt (engine API, read-only). Default false.")}
            });
            tool.affinity = ThreadAffinity::Worker;
            tool.readOnly = true;
            tool.handler = [context](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                const bool includeLib = reader.optBool("includeLib", false);
                const fs::path root = scriptsRoot(context);

                nlohmann::json scripts = nlohmann::json::array();
                auto collect = [&](const fs::path& dir)
                {
                    std::error_code ec;
                    if (!fs::is_directory(dir, ec))
                    {
                        return;
                    }
                    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
                    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
                    {
                        std::error_code entryEc;
                        if (!it->is_regular_file(entryEc) || !detail::componentEquals(it->path().extension(), ".mt"))
                        {
                            continue;
                        }
                        const std::uintmax_t bytes = it->file_size(entryEc);
                        scripts.push_back({
                            {"path", relativeToRoot(root, it->path())},
                            {"bytes", entryEc ? 0 : bytes}
                        });
                    }
                };

                collect(root / "game");
                if (includeLib)
                {
                    collect(root / "lib");
                }

                std::sort(scripts.begin(), scripts.end(), [](const nlohmann::json& a, const nlohmann::json& b)
                {
                    return a["path"].get<std::string>() < b["path"].get<std::string>();
                });

                return ToolResult::ok({{"scriptsRoot", pathToUtf8(root)}, {"scripts", std::move(scripts)}});
            };
            registry.add(std::move(tool));
        }

        void registerScriptRead(ToolRegistry& registry, const ToolContext& context)
        {
            ToolDef tool;
            tool.name = "script_read";
            tool.title = "Read script";
            tool.description =
                "Read an mType script's source. 'path' is relative to the scripts root, e.g. "
                "'game/PlayerController.mt' or 'lib/engine/Log.mt'. Returns {path, source}.";
            tool.inputSchema = schema::object({
                {"path", schema::string("Script path relative to the scripts root (must end in .mt)")}
            }, {"path"});
            tool.affinity = ThreadAffinity::Worker;
            tool.readOnly = true;
            tool.handler = [context](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                const std::string path = reader.requireString("path");
                const fs::path root = scriptsRoot(context);
                const fs::path file = resolveScript(root, path, false);

                std::error_code ec;
                if (!fs::is_regular_file(file, ec))
                {
                    return ToolResult::error("Script not found: " + path + " (use script_list)");
                }
                const std::uintmax_t bytes = fs::file_size(file, ec);
                if (ec || bytes > maxScriptBytes)
                {
                    return ToolResult::error("Script is unreadable or larger than 1 MB: " + path);
                }

                std::ifstream stream(file, std::ios::binary);
                if (!stream)
                {
                    return ToolResult::error("Failed to open script: " + path);
                }
                std::string source((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

                return ToolResult::ok({{"path", relativeToRoot(root, file)}, {"source", std::move(source)}});
            };
            registry.add(std::move(tool));
        }

        void registerScriptWrite(ToolRegistry& registry, const ToolContext& context)
        {
            ToolDef tool;
            tool.name = "script_write";
            tool.title = "Write script";
            tool.description =
                "Create or overwrite an mType game script. 'path' is relative to the scripts root and must be "
                "under game/ with a .mt extension (folders are created). Call scripts_build afterwards: "
                "writing does NOT recompile, and play_start only builds when nothing is compiled yet.\n"
                "mType primer: a behaviour is a class annotated @Script with lifecycle methods "
                "onStart(), onUpdate(float deltaTime), onFixedUpdate(float dt), onLateUpdate(float dt), "
                "onDestroy(). Import the engine API with paths relative to the file, e.g. from "
                "game/Foo.mt: import * from \"../lib/engine/Log.mt\"; (one more ../ per sub-folder). "
                "Declarations are typed (int, float, bool, string, class types) — no 'var'; 'match' is a "
                "keyword; circular imports are errors. Minimal reference: script_read game/SimpleTest.mt; "
                "fuller example: game/PlayerController.mt; API: script_list includeLib=true.\n"
                "Example:\n"
                "import * from \"../lib/engine/Log.mt\";\n"
                "@Script\n"
                "public class Spinner {\n"
                "    private float elapsed = 0.0;\n"
                "    public function onStart(): void { Log::info(\"Spinner started\"); }\n"
                "    public function onUpdate(float deltaTime): void { elapsed = elapsed + deltaTime; }\n"
                "}";
            tool.inputSchema = schema::object({
                {"path", schema::string("Path relative to the scripts root, under game/, ending in .mt "
                                        "(e.g. 'game/player/Mover.mt')")},
                {"source", schema::string("Full mType source text (UTF-8)")}
            }, {"path", "source"});
            tool.affinity = ThreadAffinity::Worker;
            tool.destructive = true;
            tool.handler = [context](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                const std::string path = reader.requireString("path");
                const std::string source = reader.requireString("source");
                if (source.size() > maxScriptBytes)
                {
                    throw ArgError("argument 'source' exceeds 1 MB");
                }

                const fs::path root = scriptsRoot(context);
                const fs::path file = resolveScript(root, path, true);

                std::error_code ec;
                const bool existed = fs::exists(file, ec);
                if (existed && !fs::is_regular_file(file, ec))
                {
                    return ToolResult::error("Path exists and is not a regular file: " + path);
                }
                fs::create_directories(file.parent_path(), ec);
                if (ec)
                {
                    return ToolResult::error("Failed to create folder for " + path + ": " + ec.message());
                }

                // Write beside the target then rename over it, so a failed write never
                // leaves a truncated script for the next build to choke on.
                fs::path temp = file;
                temp += ".mcp-tmp";
                {
                    std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
                    if (!stream)
                    {
                        return ToolResult::error("Failed to open " + path + " for writing");
                    }
                    stream.write(source.data(), static_cast<std::streamsize>(source.size()));
                    stream.close();
                    if (!stream)
                    {
                        fs::remove(temp, ec);
                        return ToolResult::error("Failed to write " + path);
                    }
                }
                fs::rename(temp, file, ec);
                if (ec)
                {
                    std::error_code removeEc;
                    fs::remove(temp, removeEc);
                    return ToolResult::error("Failed to replace " + path + ": " + ec.message());
                }

                return ToolResult::ok({
                    {"path", relativeToRoot(root, file)},
                    {"bytes", source.size()},
                    {"created", !existed},
                    {"next", "run scripts_build to compile"}
                });
            };
            registry.add(std::move(tool));
        }

        void registerScriptsBuild(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "scripts_build";
            tool.title = "Build scripts";
            tool.description =
                "Compile all game scripts (game/**/*.mt) synchronously. Not allowed during Play mode. "
                "Returns {success, filesCompiled, filesFailed, errors[]}; each error is the mType builder "
                "message '<source file>: <diagnostic>'. Fix the reported files with script_write and build again.";
            tool.affinity = ThreadAffinity::Main;
            tool.timeout = std::chrono::milliseconds(120000);
            tool.handler = [](const nlohmann::json&) -> ToolResult
            {
                auto& dispatcher = events::EventDispatcher::instance();
                // A rebuild resets the interpreter under live script instances.
                if (dispatcher.query(events::editor::IsPlayModeQuery{}))
                {
                    return ToolResult::error("Cannot build scripts during Play mode; call play_stop first");
                }

                services::ScriptBuildResult result = dispatcher.execute(events::scripting::BuildScriptsCommand{});

                nlohmann::json out{
                    {"success", result.success},
                    {"filesCompiled", result.filesCompiled},
                    {"filesFailed", result.filesFailed},
                    {"errors", result.errors}
                };

                std::string text;
                if (result.success)
                {
                    text = "Build succeeded: " + std::to_string(result.filesCompiled) + " file(s) compiled.";
                }
                else
                {
                    text = "Build FAILED (" + std::to_string(result.filesFailed) + " file(s) failed, " +
                           std::to_string(result.errors.size()) + " error(s)):";
                    for (const std::string& error : result.errors)
                    {
                        text += "\n- " + error;
                    }
                }
                return ToolResult::ok(std::move(out), std::move(text));
            };
            registry.add(std::move(tool));
        }

        void registerScriptAttach(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "script_attach";
            tool.title = "Attach script";
            tool.description =
                "Attach a game script to an entity (adds a ScriptComponent entry). 'path' is relative to the "
                "scripts root and must be under game/. Scripts are instantiated on play_start; build first "
                "with scripts_build. Returns {entity, scripts[]} (the entity's attached scripts).";
            tool.inputSchema = schema::object({
                {"entity", schema::entity()},
                {"path", schema::string("Script path relative to the scripts root, e.g. 'game/Spinner.mt'")},
                {"enabled", schema::boolean("Start enabled. Default true.")},
                {"inputPriority", schema::integer("Execution / input order, higher runs first. Default 0.")}
            }, {"entity", "path"});
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                const services::EntityHandle entity = requireLiveEntity(reader, "entity");
                const std::string path = reader.requireString("path");
                const fs::path root = scriptsRootOnMain();
                const fs::path file = resolveScript(root, path, true);

                std::error_code ec;
                if (!fs::is_regular_file(file, ec))
                {
                    return ToolResult::error("Script not found: " + path + " (create it with script_write)");
                }

                events::scripting::AttachScriptCommand command;
                command.entity = entity;
                // Absolute, like the editor's file-dialog attach; narrow like every
                // other engine path string (AssetRef::fromPath builds fs::path from it).
                command.data.scriptPath = file.string();
                command.data.enabled = reader.optBool("enabled", true);
                command.data.inputPriority = static_cast<int>(reader.optInt("inputPriority", 0));

                if (!events::EventDispatcher::instance().execute(command))
                {
                    return ToolResult::error("Script '" + path + "' is already attached to entity " +
                                             std::to_string(static_cast<uint32_t>(entity.id)));
                }

                return ToolResult::ok({
                    {"entity", static_cast<uint32_t>(entity.id)},
                    {"scripts", attachedScripts(entity, root)}
                });
            };
            registry.add(std::move(tool));
        }

        void registerScriptDetach(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "script_detach";
            tool.title = "Detach script";
            tool.description =
                "Detach a script from an entity. 'path' is relative to the scripts root (e.g. "
                "'game/Spinner.mt'). The ScriptComponent is removed with its last script. "
                "Returns {entity, scripts[]} (the remaining scripts).";
            tool.inputSchema = schema::object({
                {"entity", schema::entity()},
                {"path", schema::string("Script path relative to the scripts root")}
            }, {"entity", "path"});
            tool.destructive = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                const services::EntityHandle entity = requireLiveEntity(reader, "entity");
                const std::string path = reader.requireString("path");
                const fs::path root = scriptsRootOnMain();
                const fs::path target = resolveScript(root, path, false);

                auto& dispatcher = events::EventDispatcher::instance();
                events::scripting::GetScriptPathsQuery query;
                query.entity = entity;
                const std::vector<std::string> stored = dispatcher.query(query);

                // Detach matches the stored string exactly, so find the entry that
                // refers to the same file and pass its string through verbatim.
                const fs::path assetsRoot = root.parent_path();
                auto match = std::find_if(stored.begin(), stored.end(), [&](const std::string& candidate)
                {
                    return sameScriptPath(assetsRoot, candidate, target);
                });
                if (match == stored.end())
                {
                    return ToolResult::error("Script '" + path + "' is not attached to entity " +
                                             std::to_string(static_cast<uint32_t>(entity.id)) +
                                             "; attached: " + attachedScripts(entity, root).dump());
                }

                events::scripting::DetachScriptCommand command;
                command.entity = entity;
                command.scriptPath = *match;
                dispatcher.execute(command);

                return ToolResult::ok({
                    {"entity", static_cast<uint32_t>(entity.id)},
                    {"scripts", attachedScripts(entity, root)}
                });
            };
            registry.add(std::move(tool));
        }
    }

    void registerScriptTools(ToolRegistry& registry, const ToolContext& context)
    {
        registerScriptList(registry, context);
        registerScriptRead(registry, context);
        registerScriptWrite(registry, context);
        registerScriptsBuild(registry);
        registerScriptAttach(registry);
        registerScriptDetach(registry);
    }
}

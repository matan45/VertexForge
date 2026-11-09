#include "CoreTools.hpp"
#include "ExportTracker.hpp"
#include "PathSandbox.hpp"
#include "../protocol/ArgReader.hpp"

#include "events/EventDispatcher.hpp"
#include "events/project/ExportEvents.hpp"
#include "events/project/ProjectEvents.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace mcp::tools
{
    namespace
    {
        namespace fs = std::filesystem;

        // Script build + material recompile run synchronously on the main thread
        // inside ExportGameCommand before the export thread spawns.
        constexpr std::chrono::milliseconds exportDispatchTimeout{300000};
        constexpr std::chrono::milliseconds exportToolTimeout{310000};

        // Main thread only. A clean build must not target the project directory (the
        // .vfproject's folder) or the asset root, nor any folder that contains them.
        // `output` is already weakly canonical. No project loaded = nothing to protect;
        // ExportGameCommand then refuses on its own.
        std::optional<std::string> cleanBuildRejectionOnMain(const fs::path& output)
        {
            auto& dispatcher = events::EventDispatcher::instance();

            // Both are built with path::string() (narrow) by ProjectServiceImpl.
            std::vector<fs::path> protectedDirectories;
            auto project = dispatcher.query(events::project::GetCurrentProjectQuery{});
            if (project.has_value() && !project->workingDirectory.empty())
            {
                protectedDirectories.emplace_back(project->workingDirectory);
            }
            auto projectFile = dispatcher.query(events::project::GetProjectPathQuery{});
            if (projectFile.has_value() && !projectFile->empty())
            {
                protectedDirectories.push_back(fs::path(*projectFile).parent_path());
            }

            for (const fs::path& directory : protectedDirectories)
            {
                std::error_code ec;
                const fs::path canonical = fs::weakly_canonical(fs::absolute(directory, ec), ec);
                if (ec || canonical.empty())
                {
                    continue;
                }
                if (detail::isWithin(output, canonical))
                {
                    return "cleanBuild refused: '" + pathToUtf8(output) + "' is the project directory or contains "
                           "it ('" + pathToUtf8(canonical) + "'); choose a separate output folder";
                }
            }
            return std::nullopt;
        }

        void registerGameExport(ToolRegistry& registry, const ToolContext& context,
                                const std::shared_ptr<ExportTracker>& tracker)
        {
            ToolDef tool;
            tool.name = "game_export";
            tool.title = "Export game";
            tool.description =
                "Start a standalone game export of the loaded project (same as the editor's Export dialog) into "
                "'outputDirectory' (absolute path; created if missing). "
                "Before returning, the editor builds the project scripts (when buildScripts) and "
                "recompiles stale graph materials on its main thread, which can take minutes; a failure there "
                "aborts the export and is returned as the error. Then the export continues in the background: "
                "returns {started, outputDirectory} and you poll game_export_status until state is 'done'. "
                "Errors when an export is already running or no project is loaded. cleanBuild discards cached "
                "intermediates and is refused when outputDirectory is the project directory or one of its parents.";
            tool.inputSchema = schema::object({
                {"outputDirectory", schema::string("Absolute destination folder for the exported game")},
                {"cleanBuild", schema::boolean("Discard cached intermediates from a previous export (default false)")},
                {"verifyIntegrity", schema::boolean("Verify the written archives after packing (default true)")},
                {"buildScripts", schema::boolean("Build the project scripts before exporting (default true)")},
                {"stripUnreferencedAssets", schema::boolean("Ship only assets reachable from the scenes (default false)")},
                {"alwaysIncludePatterns", schema::array(schema::string("Glob pattern relative to the asset root"),
                                                        "Assets to ship even when stripUnreferencedAssets drops them")}
            }, {"outputDirectory"});
            tool.affinity = ThreadAffinity::Worker;
            tool.timeout = exportToolTimeout;
            tool.destructive = true;
            tool.handler = [context, tracker](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                const std::string outputArg = reader.requireString("outputDirectory");
                if (outputArg.empty())
                {
                    throw ArgError("argument 'outputDirectory' must not be empty");
                }
                if (outputArg.find('\0') != std::string::npos)
                {
                    throw ArgError("argument 'outputDirectory' contains a NUL character");
                }
                const fs::path requested = pathFromUtf8(outputArg);
                if (!requested.is_absolute())
                {
                    throw ArgError("argument 'outputDirectory' must be an absolute path");
                }
                std::error_code ec;
                const fs::path output = fs::weakly_canonical(requested, ec);
                if (ec || output.empty())
                {
                    throw ArgError("cannot resolve outputDirectory '" + outputArg + "'");
                }

                events::gameExport::ExportGameCommand command;
                // Engine paths are narrow strings (the Export dialog passes them the same way).
                command.outputDirectory = output.string();
                command.cleanBuild = reader.optBool("cleanBuild", false);
                command.verifyIntegrity = reader.optBool("verifyIntegrity", true);
                command.buildScripts = reader.optBool("buildScripts", true);
                command.stripUnreferencedAssets = reader.optBool("stripUnreferencedAssets", false);
                if (reader.has("alwaysIncludePatterns"))
                {
                    command.alwaysIncludePatterns = reader.requireStringArray("alwaysIncludePatterns");
                }

                // The tracker is not reset here: when an export is already running the
                // command refuses without publishing, and the live export's status must
                // survive. A new export resets it through ExportStartedNotification, which
                // the command publishes synchronously - so startCount tells whether this
                // call got that far (a pre-thread failure publishes Started then Completed).
                nlohmann::json outcome;
                try
                {
                    outcome = context.runOnMain([tracker, command, output]() -> nlohmann::json
                    {
                        if (command.cleanBuild)
                        {
                            if (auto rejection = cleanBuildRejectionOnMain(output))
                            {
                                return {{"error", *rejection}};
                            }
                        }

                        const uint64_t startsBefore = tracker->snapshot().startCount;
                        if (events::EventDispatcher::instance().execute(command))
                        {
                            return {{"started", true}};
                        }

                        const ExportTracker::Snapshot after = tracker->snapshot();
                        if (after.startCount != startsBefore && after.state == ExportTracker::State::Done && !after.success)
                        {
                            return {{"error", "export failed before it started: " + after.error}};
                        }
                        return {{"error", "export not started: an export is already running or no project is loaded"}};
                    }, exportDispatchTimeout);
                }
                catch (const MainThreadTimeout&)
                {
                    // A task the main thread had not started is cancelled; one it had started
                    // (script build / material recompile) still completes and may export.
                    return ToolResult::error("timed out waiting for the editor to start the export; if it had begun the "
                                             "script build or material recompile it continues - poll game_export_status");
                }

                if (outcome.contains("error"))
                {
                    return ToolResult::error(outcome.at("error").get<std::string>());
                }
                return ToolResult::ok({
                    {"started", true},
                    {"outputDirectory", pathToUtf8(output)},
                    {"hint", "poll game_export_status until state is 'done'"}
                });
            };
            registry.add(std::move(tool));
        }

        void registerGameExportStatus(ToolRegistry& registry, const std::shared_ptr<ExportTracker>& tracker)
        {
            ToolDef tool;
            tool.name = "game_export_status";
            tool.title = "Export status";
            tool.description =
                "Status of the most recent game export (started by game_export or the editor's Export dialog): "
                "{state: 'idle' | 'preparing' | 'exporting' | 'done', progress (0-1), step, outputDirectory}; "
                "once done also {success, error, warnings, outputPath}.";
            tool.inputSchema = schema::object(nlohmann::json::object());
            // Reads only the tracker under its mutex - no dispatcher call, so no main-thread hop.
            tool.affinity = ThreadAffinity::Worker;
            tool.readOnly = true;
            tool.handler = [tracker](const nlohmann::json&) -> ToolResult
            {
                return ToolResult::ok(exportStatusToJson(tracker->snapshot()));
            };
            registry.add(std::move(tool));
        }
    }

    void registerExportTools(ToolRegistry& registry, const ToolContext& context)
    {
        // Owned by the two tool handlers, so it lives as long as the registry. The
        // export thread may outlive it; its callbacks only hold a weak_ptr.
        const std::shared_ptr<ExportTracker> tracker = ExportTracker::create();
        registerGameExport(registry, context, tracker);
        registerGameExportStatus(registry, tracker);
    }
}

#include "CoreTools.hpp"
#include "EQSToolSupport.hpp"
#include "PathSandbox.hpp"
#include "SettingsTable.hpp"
#include "ToolHelpers.hpp"

#include "events/EventDispatcher.hpp"
#include "events/ai/EQSAuthoringEvents.hpp"
#include "events/ai/EQSEvents.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "events/scene/SceneSettingsEvents.hpp"
#include "math/TransformUtils.hpp"

#include <glm/glm.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

// VK-1660: eqs_list_queries, eqs_get_query, eqs_set_query and eqs_run - the EQS query templates
// (assets/eqs/<name>.vfEQSQuery) and a one-off run with every candidate's detail. Templates are read and written
// through the EQS authoring events (EQSAuthoringService: strict, hash-guarded, atomic) at the path the engine
// looks them up at (GetEQSQueryInfoQuery), so a tool never writes a file no query reads. Not undoable.
namespace mcp::tools
{
    namespace
    {
        namespace fs = std::filesystem;

        // The engine's template path is the same folder for every name; listing asks it for this one to report
        // the folder even when it holds no template yet.
        constexpr const char* folderProbeName = "Template";

        // Main thread. The project root (ProjectConfig::workingDirectory), absolute; agent paths are relative to it.
        fs::path projectRootOnMain()
        {
            auto project = events::EventDispatcher::instance().query(events::project::GetCurrentProjectQuery{});
            if (!project.has_value() || project->workingDirectory.empty())
            {
                throw std::runtime_error("No project is loaded; open one with project_open first");
            }
            // workingDirectory is built with path::string() (narrow) by ProjectServiceImpl.
            return fs::absolute(fs::path(project->workingDirectory));
        }

        // Main thread. The engine's view of the template `name` (EQSServiceImpl -> EQSAdapter::lookupQuery):
        // where it is looked up (a narrow engine path) and whether it exists and is valid as the engine cached it.
        services::EQSQueryInfo queryInfoOnMain(const std::string& name)
        {
            events::ai::GetEQSQueryInfoQuery query;
            query.queryName = name;
            services::EQSQueryInfo info = events::EventDispatcher::instance().query(query);
            if (info.path.empty())
            {
                throw std::runtime_error("The engine has no template path for the EQS query '" + name + "'");
            }
            return info;
        }

        // Main thread. The template file's spec, for the run's filter / score purposes; nullopt when the file
        // cannot be read or is invalid.
        std::optional<eqs::EQSQuerySpec> readTemplateSpecOnMain(const std::string& enginePath)
        {
            events::ai::LoadEQSQueryAssetQuery load;
            load.path = enginePath;
            services::EQSAssetLoadResult loaded;
            try
            {
                loaded = events::EventDispatcher::instance().query(load);
            }
            catch (const std::runtime_error&)
            {
                return std::nullopt; // no EQS authoring service in this binary
            }
            if (!loaded.success)
            {
                return std::nullopt;
            }
            eqs::EQSSpecParseResult parsed = eqs::parseQuerySpecText(loaded.text);
            return std::move(parsed.spec);
        }

        // Any thread. `path` relative to `root` with '/' separators, or the whole path when it lies outside it.
        std::string projectRelative(const fs::path& root, const fs::path& path)
        {
            std::error_code ec;
            const fs::path relative = fs::relative(path, root, ec);
            if (ec || relative.empty() || *relative.begin() == "..")
            {
                return genericPathToUtf8(path);
            }
            return genericPathToUtf8(relative);
        }

        // Any thread. The text of a template file, or nullopt with `error` set.
        std::optional<std::string> readTemplateText(const fs::path& path, std::string& error)
        {
            std::error_code ec;
            if (!fs::is_regular_file(path, ec))
            {
                error = "the file is missing";
                return std::nullopt;
            }
            const std::uintmax_t size = fs::file_size(path, ec);
            if (ec)
            {
                error = "the file could not be read: " + ec.message();
                return std::nullopt;
            }
            if (size > eqstool::maxTemplateBytes)
            {
                error = std::format("the file is too large ({} bytes)", size);
                return std::nullopt;
            }
            std::string text(static_cast<std::size_t>(size), '\0');
            std::ifstream stream(path, std::ios::binary);
            if (!stream.is_open() || (size > 0 && !stream.read(text.data(), static_cast<std::streamsize>(size))))
            {
                error = "the file could not be read";
                return std::nullopt;
            }
            return text;
        }

        // What is left of a Worker tool's deadline, as a runOnMain timeout (at least 1 ms).
        std::chrono::milliseconds remainingUntil(std::chrono::steady_clock::time_point deadline)
        {
            const auto left =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            return std::max(left, std::chrono::milliseconds(1));
        }

        bool sceneLoadPending()
        {
            try
            {
                return events::EventDispatcher::instance().query(events::scene::IsSceneLoadPendingQuery{});
            }
            catch (const std::runtime_error&)
            {
                return false; // no scene persistence service: nothing can be loading
            }
        }

        std::string requireQueryName(const ArgReader& reader)
        {
            const std::string name = reader.requireString("name");
            if (!eqs::isValidQueryName(name))
            {
                throw ArgError("argument 'name' must be a template name: a letter, then up to 63 letters, digits or "
                               "'_' (got '" + name + "')");
            }
            return name;
        }

        nlohmann::json nameSchema(std::string description)
        {
            nlohmann::json out = schema::string(std::move(description));
            out["pattern"] = std::string(eqstool::queryNamePattern);
            return out;
        }

        // The +Z axis of a world rotation on the ground plane, as the BT adapter builds EQSContext::querierForward;
        // nullopt when it points straight up or down.
        std::optional<glm::vec3> groundForward(const services::TransformData& world)
        {
            // TransformData rotations are XYZ Euler degrees (math::decomposeMatrix / composeMatrix).
            const glm::mat4 rotation = math::composeMatrix(glm::vec3(0.0f), world.rotation, glm::vec3(1.0f));
            const glm::vec3 forward(rotation[2].x, 0.0f, rotation[2].z);
            const float lengthSquared = glm::dot(forward, forward);
            if (!(lengthSquared > 1.0e-8f))
            {
                return std::nullopt;
            }
            return forward / std::sqrt(lengthSquared);
        }

        // The querier's navigation size for PathCost tests, when it has a NavmeshAgent.
        std::optional<services::NavmeshAgentData> navmeshAgentOf(const services::EntityHandle& entity)
        {
            events::scene::GetNavmeshAgentDataQuery query;
            query.entity = entity;
            try
            {
                return events::EventDispatcher::instance().query(query);
            }
            catch (const std::runtime_error&)
            {
                return std::nullopt; // no physics component service in this binary
            }
        }

        // "entity 12 'Enemy'" for an eqs_run entity {id, name}.
        std::string entityLabel(const nlohmann::json& entity)
        {
            std::string label = "entity " + (entity.contains("id") ? entity.at("id").dump() : std::string("?"));
            const auto name = entity.find("name");
            if (name != entity.end() && name->is_string())
            {
                label += " '" + name->get<std::string>() + "'";
            }
            return label;
        }

        std::string runSummary(const std::string& name, const eqs::EQSDebugResult& run, const nlohmann::json& out)
        {
            if (run.result.status != eqs::EQSQueryStatus::Completed)
            {
                return std::format("EQS query '{}' {}: {}", name,
                                   run.result.status == eqs::EQSQueryStatus::Failed
                                       ? std::string("failed")
                                       : std::string(eqstool::statusName(run.result.status)),
                                   run.result.error.empty() ? std::string("no reason given") : run.result.error);
            }

            std::string summary = std::format("EQS query '{}' ran in {} ms: ", name, out.at("timeMs").get<double>());
            const nlohmann::json& best = out.at("best");
            if (best.is_null())
            {
                summary += "no result";
            }
            else if (best.contains("entity"))
            {
                summary += std::format("best is {} (score {})", entityLabel(best.at("entity")), best.at("score").dump());
            }
            else
            {
                summary += std::format("best is the point {} (score {})", best.at("position").dump(),
                                       best.at("score").dump());
            }
            const nlohmann::json& counts = out.at("counts");
            summary += std::format("; {} generated, {} kept", counts.at("generated").get<std::size_t>(),
                                   counts.at("scored").get<std::size_t>());
            const nlohmann::json& filteredBy = counts.at("filteredBy");
            for (auto it = filteredBy.begin(); it != filteredBy.end(); ++it)
            {
                summary += std::format(", {} dropped by {}", it.value().get<std::size_t>(), it.key());
            }
            summary += ".";
            return summary;
        }

        void registerEQSListQueries(ToolRegistry& registry, const ToolContext& context)
        {
            ToolDef tool;
            tool.name = "eqs_list_queries";
            tool.title = "List EQS queries";
            tool.description =
                "List the project's EQS (Environment Query System) query templates, assets/eqs/<name>.vfEQSQuery. A "
                "template picks a point or an entity for an AI: a generator makes candidates (points on rings or a "
                "grid, navmesh samples, or entities with a given name) around the querier or its target, filter tests "
                "drop candidates and score tests rank the rest. Behavior-tree EnvironmentQuery / EQSRefresh nodes and "
                "scripts (EQS.mt) run templates by name. Returns {directory, queries:[{name, path, valid, error?, "
                "description, generator, tests, maxResults, summary}], catalog}: catalog (left out with "
                "includeCatalog=false) lists every generator and test type with its params (kind, default, range, "
                "values), the filter and score rules and the limits. Read a template with eqs_get_query, write one "
                "with eqs_set_query, try one with eqs_run; vf://docs/bt explains EQS.";
            tool.inputSchema = closedObject({
                {"includeCatalog", schema::boolean("Also return the catalogue of generator and test types, their params "
                                                   "and the scoring rules. Default true.")}
            });
            // Worker: the template files are read on the connection thread; one hop fetches the listing.
            tool.affinity = ThreadAffinity::Worker;
            tool.timeout = eqstool::listDeadline;
            tool.readOnly = true;
            tool.handler = [context](const nlohmann::json& args) -> ToolResult
            {
                // The server enforces no Worker timeout, so this tool keeps its own.
                const auto deadline = std::chrono::steady_clock::now() + eqstool::listDeadline;
                env::rejectUnknownArgs(args, {"includeCatalog"}, "eqs_list_queries");
                const ArgReader reader(args);
                const bool includeCatalog = reader.optBool("includeCatalog", true);

                const nlohmann::json listing = context.runOnMain([]() -> nlohmann::json
                {
                    const fs::path root = projectRootOnMain();
                    const fs::path folder = fs::path(queryInfoOnMain(folderProbeName).path).parent_path();
                    nlohmann::json templates = nlohmann::json::array();
                    // Every valid template name in the engine's folder, sorted (EQSServiceImpl::listQueries).
                    const std::vector<services::EQSQueryInfo> infos =
                        events::EventDispatcher::instance().query(events::ai::ListEQSQueriesQuery{});
                    for (const services::EQSQueryInfo& info : infos)
                    {
                        nlohmann::json entry = nlohmann::json::object();
                        entry["name"] = info.name;
                        entry["path"] = pathToUtf8(fs::path(info.path));
                        templates.push_back(std::move(entry));
                    }
                    nlohmann::json out = nlohmann::json::object();
                    out["root"] = pathToUtf8(root);
                    out["folder"] = pathToUtf8(folder);
                    out["templates"] = std::move(templates);
                    return out;
                }, remainingUntil(deadline));

                const fs::path root = pathFromUtf8(listing.at("root").get<std::string>());
                const fs::path folder = pathFromUtf8(listing.at("folder").get<std::string>());
                const nlohmann::json& templates = listing.at("templates");

                nlohmann::json queries = nlohmann::json::array();
                std::size_t invalid = 0;
                for (const nlohmann::json& item : templates)
                {
                    if (std::chrono::steady_clock::now() >= deadline)
                    {
                        throw std::runtime_error(std::format(
                            "eqs_list_queries ran past its {} s deadline after reading {} of {} templates; retry",
                            std::chrono::duration_cast<std::chrono::seconds>(eqstool::listDeadline).count(),
                            queries.size(), templates.size()));
                    }
                    const fs::path file = pathFromUtf8(item.at("path").get<std::string>());
                    std::string readError;
                    const std::optional<std::string> text = readTemplateText(file, readError);
                    nlohmann::json entry = eqstool::listEntryJson(item.at("name").get<std::string>(),
                                                                  projectRelative(root, file), text, readError);
                    if (!entry.at("valid").get<bool>())
                    {
                        ++invalid;
                    }
                    queries.push_back(std::move(entry));
                }

                const std::string directory = projectRelative(root, folder);
                std::string summary = std::format("{} EQS query template{} in {}", queries.size(),
                                                  queries.size() == 1 ? "" : "s", directory);
                if (invalid > 0)
                {
                    summary += std::format(" ({} invalid)", invalid);
                }
                summary += ".";

                nlohmann::json out = nlohmann::json::object();
                out["directory"] = directory;
                out["queries"] = std::move(queries);
                if (includeCatalog)
                {
                    out["catalog"] = eqstool::catalogJson();
                }
                return reply(std::move(out), std::move(summary));
            };
            registry.add(std::move(tool));
        }

        void registerEQSGetQuery(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "eqs_get_query";
            tool.title = "Read EQS query";
            tool.description =
                "Read one EQS query template by name. Returns {name, path, contentHash, valid, query, summary}: query "
                "is the template in canonical form (every param written out, enums in their canonical spelling) - "
                "edit it and pass it back to eqs_set_query with this contentHash as expectedContentHash. An invalid "
                "file returns valid=false, its errors [{path, message}] and its text instead of query.";
            tool.inputSchema = closedObject({
                {"name", nameSchema("Template name (the file stem), e.g. 'NearestEnemy'")}
            }, {"name"});
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"name"}, "eqs_get_query");
                const ArgReader reader(args);
                const std::string name = requireQueryName(reader);

                const fs::path root = projectRootOnMain();
                const services::EQSQueryInfo info = queryInfoOnMain(name);
                const fs::path file(info.path);
                const std::string shown = projectRelative(root, file);
                std::error_code ec;
                if (!fs::is_regular_file(file, ec))
                {
                    throw std::runtime_error(std::format("There is no EQS query '{}' ({} does not exist); "
                                                         "eqs_list_queries lists the templates and eqs_set_query "
                                                         "creates one", name, shown));
                }

                events::ai::LoadEQSQueryAssetQuery load;
                load.path = info.path;
                const services::EQSAssetLoadResult loaded = events::EventDispatcher::instance().query(load);
                if (!loaded.success)
                {
                    throw std::runtime_error("The EQS query '" + name + "' could not be read: " + loaded.error);
                }

                nlohmann::json out = nlohmann::json::object();
                out["name"] = name;
                out["path"] = shown;
                out["contentHash"] = eqstool::hashToHex(loaded.contentHash);
                const eqs::EQSSpecParseResult parsed = eqs::parseQuerySpecText(loaded.text);
                if (parsed.spec.has_value())
                {
                    const std::string summary = eqs::summarizeQuery(*parsed.spec);
                    out["valid"] = true;
                    out["query"] = eqs::querySpecToJson(*parsed.spec);
                    out["summary"] = summary;
                    return reply(std::move(out), std::format("EQS query '{}' ({}): {}", name, shown, summary));
                }

                out["valid"] = false;
                out["errors"] = eqstool::specErrorsJson(parsed.errors);
                out["errorCount"] = parsed.errors.size();
                out["text"] = loaded.text;
                return reply(std::move(out),
                             std::format("EQS query '{}' ({}) is invalid, {} error(s):\n{}\nFix it with eqs_set_query "
                                         "(pass contentHash as expectedContentHash).",
                                         name, shown, parsed.errors.size(), eqstool::specErrorLines(parsed.errors)));
            };
            registry.add(std::move(tool));
        }

        void registerEQSSetQuery(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "eqs_set_query";
            tool.title = "Write EQS query";
            tool.description =
                "Create or replace the EQS query template assets/eqs/<name>.vfEQSQuery. 'query' is the template: "
                "{version?: 1, description?, generator: {type, params?}, tests: [{type, params?, filter: {min?, max?} "
                "| score: {mode?, weight?, combine?}}], maxResults?: 1-64}; eqs_list_queries' catalog lists the "
                "types, params, defaults and ranges. It is checked strictly (unknown keys, types and params, values "
                "of the wrong kind or out of range, a test with both or neither of filter / score, a generator above "
                "the candidate limit) and every error is reported with its JSON path inside 'query'; nothing is "
                "written then. createNew=true refuses an existing template; expectedContentHash (from eqs_get_query) "
                "refuses one that changed since it was read. Saved in canonical form; behavior trees and scripts use "
                "it from their next query, also in Play mode. Not undoable. Returns {name, path, contentHash, "
                "created, warnings}. Example, the nearest entity named Enemy: {\"generator\": {\"type\": "
                "\"Entities\", \"params\": {\"name\": \"Enemy\", \"radius\": 25}}, \"tests\": [{\"type\": "
                "\"Distance\", \"score\": {\"mode\": \"InverseLinear\"}}], \"maxResults\": 1}.";
            tool.inputSchema = closedObject({
                {"name", nameSchema("Template name: a letter, then up to 63 letters, digits or '_' (the file stem)")},
                {"query", schema::anyObject("The template: {version?: 1, description?, generator: {type, params?}, "
                                            "tests: [{type, params?, filter | score}], maxResults?}")},
                {"expectedContentHash", schema::string("contentHash from eqs_get_query: refuse to replace a template "
                                                       "that changed since it was read")},
                {"createNew", schema::boolean("true: refuse to replace an existing template. Default false.")}
            }, {"name", "query"});
            tool.timeout = eqstool::setTimeout;
            tool.destructive = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"name", "query", "expectedContentHash", "createNew"}, "eqs_set_query");
                const ArgReader reader(args);
                const std::string name = requireQueryName(reader);
                const nlohmann::json& document = reader.raw("query");
                if (!document.is_object())
                {
                    throw ArgError("argument 'query' must be the template object {version?, description?, generator, "
                                   "tests, maxResults?}");
                }
                std::optional<uint64_t> expectedHash;
                if (reader.has("expectedContentHash"))
                {
                    const std::string text = reader.requireString("expectedContentHash");
                    expectedHash = eqstool::hexToHash(text);
                    if (!expectedHash.has_value())
                    {
                        throw ArgError("argument 'expectedContentHash' must be the hex contentHash eqs_get_query "
                                       "returned (got '" + text + "')");
                    }
                }
                const bool createNew = reader.optBool("createNew", false);
                if (createNew && expectedHash.has_value())
                {
                    throw ArgError("createNew and expectedContentHash exclude each other: createNew only creates a "
                                   "template, expectedContentHash only replaces the one it was read from");
                }

                const eqs::EQSSpecParseResult parsed = eqs::parseQuerySpec(document);
                if (!parsed.spec.has_value())
                {
                    throw ArgError(std::format("the query has {} error(s), so nothing was written (paths are inside "
                                               "'query'):\n{}",
                                               parsed.errors.size(), eqstool::specErrorLines(parsed.errors)));
                }

                auto& dispatcher = events::EventDispatcher::instance();
                const bool playing = dispatcher.query(events::editor::IsPlayModeQuery{});
                const fs::path root = projectRootOnMain();
                const services::EQSQueryInfo info = queryInfoOnMain(name);
                const fs::path file(info.path);
                const std::string shown = projectRelative(root, file);

                // The authoring service refuses these too; checked here to say what to do instead.
                std::error_code ec;
                const bool exists = fs::exists(file, ec);
                if (createNew && exists)
                {
                    throw std::runtime_error(std::format(
                        "The EQS query '{}' already exists ({}); createNew never replaces a template - read it with "
                        "eqs_get_query and pass its contentHash as expectedContentHash to replace it", name, shown));
                }
                if (expectedHash.has_value() && !exists)
                {
                    throw std::runtime_error(std::format(
                        "The EQS query '{}' does not exist ({}), so expectedContentHash cannot match; leave it out to "
                        "create the template", name, shown));
                }
                if (expectedHash.has_value())
                {
                    events::ai::LoadEQSQueryAssetQuery load;
                    load.path = info.path;
                    const services::EQSAssetLoadResult current = dispatcher.query(load);
                    if (!current.success)
                    {
                        throw std::runtime_error("The EQS query '" + name + "' could not be read: " + current.error);
                    }
                    if (current.contentHash != *expectedHash)
                    {
                        throw std::runtime_error(std::format(
                            "The EQS query '{}' changed since it was read (its contentHash is now {}, not {}); read it "
                            "again with eqs_get_query and reapply the edit", name,
                            eqstool::hashToHex(current.contentHash), eqstool::hashToHex(*expectedHash)));
                    }
                }

                events::ai::SaveEQSQueryAssetCommand save;
                save.path = info.path;
                save.spec = *parsed.spec;
                save.expectedContentHash = expectedHash;
                save.createNew = createNew;
                const services::EQSAssetSaveResult saved = dispatcher.execute(save);
                if (!saved.success)
                {
                    if (!saved.errors.empty())
                    {
                        throw ArgError(std::format("the query has {} error(s), so nothing was written:\n{}",
                                                   saved.errors.size(), eqstool::specErrorLines(saved.errors)));
                    }
                    throw std::runtime_error("Saving the EQS query '" + name + "' failed: " + saved.error);
                }

                std::vector<std::string> warnings;
                if (playing)
                {
                    warnings.push_back("Play mode: queries submitted from now on use the new template; one already "
                                       "queued keeps the old one.");
                }
                const bool hasScore = std::any_of(parsed.spec->tests.begin(), parsed.spec->tests.end(),
                                                  [](const eqs::EQSTestDecl& test)
                                                  {
                                                      return test.config.purpose == eqs::EQSTestPurpose::Score;
                                                  });
                if (!hasScore)
                {
                    warnings.push_back("The query has no score test: every candidate the filters keep totals 1, so the "
                                       "result is the first kept candidates in generation order (Entities generates "
                                       "the nearest first).");
                }

                std::string summary = std::format("{} the EQS query '{}' ({}): {}", saved.created ? "Created" : "Saved",
                                                  name, shown, eqs::summarizeQuery(*parsed.spec));
                for (const std::string& warning : warnings)
                {
                    summary += "\nWarning: " + warning;
                }

                nlohmann::json out = nlohmann::json::object();
                out["name"] = name;
                out["path"] = shown;
                out["contentHash"] = eqstool::hashToHex(saved.contentHash);
                out["created"] = saved.created;
                out["warnings"] = std::move(warnings);
                return reply(std::move(out), std::move(summary));
            };
            registry.add(std::move(tool));
        }

        void registerEQSRun(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "eqs_run";
            tool.title = "Run EQS query";
            tool.description =
                "Run an EQS query template now for an entity and report every candidate: its position (and entity), "
                "each test's metric, its scores and total, and the filter that dropped it. 'querier' is the asking "
                "entity (its world position and facing, and its NavmeshAgent size for PathCost); 'target' is an "
                "entity id or a world point [x, y, z], needed by templates that center on or measure against the "
                "target. Works in Edit and Play mode (refused while a scene load is pending); in Edit mode physics "
                "has no entity colliders, so Trace tests see the terrain only. show=true (the default) draws the "
                "candidates in the viewport for showSeconds. Read-only. Returns {name, status, error?, best: "
                "{position, entity?, score} | null, counts: {generated, filteredBy: {test: n}, scored, results}, "
                "tests, candidates (the kept ones best first, then the dropped ones; at most maxCandidates), "
                "context, timeMs, notes}.";
            nlohmann::json target = nlohmann::json::object();
            target["description"] = "The query's target: an entity id, or a world point [x, y, z]";
            target["anyOf"] = nlohmann::json::array({
                schema::entity("Target entity id"),
                schema::vec3("Target world point [x, y, z]")
            });
            tool.inputSchema = closedObject({
                {"name", nameSchema("Template name, e.g. 'NearestEnemy'")},
                {"querier", schema::entity("The entity running the query: its world position, facing and "
                                           "NavmeshAgent size")},
                {"target", std::move(target)},
                {"maxCandidates", schema::integerRange("Candidates reported. Default 50.", 1,
                                                       eqstool::maxCandidatesLimit)},
                {"show", schema::boolean("Draw the candidates in the viewport. Default true.")},
                {"showSeconds", schema::numberRange("How long the drawing stays, in seconds. Default 10.",
                                                    eqstool::minShowSeconds, eqstool::maxShowSeconds)}
            }, {"name", "querier"});
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"name", "querier", "target", "maxCandidates", "show", "showSeconds"},
                                       "eqs_run");
                if (sceneLoadPending())
                {
                    return ToolResult::error("Cannot run an EQS query while a scene load is in progress; retry when "
                                             "the load has finished.");
                }

                const ArgReader reader(args);
                const std::string name = requireQueryName(reader);
                const auto maxCandidates = static_cast<std::size_t>(reader.optIntInRange(
                    "maxCandidates", 1, eqstool::maxCandidatesLimit, eqstool::defaultMaxCandidates));
                const bool show = reader.optBool("show", true);
                const double showSeconds = reader.optNumberInRange("showSeconds", eqstool::minShowSeconds,
                                                                   eqstool::maxShowSeconds, eqstool::defaultShowSeconds);
                const services::EntityData querier = requireEntity(reader, "querier");

                // The context the BT adapter would build for this entity (BehaviorTreeAdapter::buildEQSContext).
                eqs::EQSContext context;
                context.querierEntityId = querier.handle.id;
                context.querierPosition = querier.worldTransform.position;
                if (const std::optional<glm::vec3> forward = groundForward(querier.worldTransform))
                {
                    context.querierForward = *forward;
                }
                const std::optional<services::NavmeshAgentData> agent = navmeshAgentOf(querier.handle);
                if (agent.has_value())
                {
                    context.agentRadius = agent->radius;
                    context.agentHeight = agent->height;
                }
                // NavMeshPoints samples the same points for the same querier on every run.
                context.seed = querier.handle.id;

                nlohmann::json targetJson = nullptr;
                if (reader.has("target"))
                {
                    const nlohmann::json& value = reader.raw("target");
                    if (value.is_number())
                    {
                        const services::EntityData target = requireEntity(reader, "target");
                        context.hasTarget = true;
                        context.targetEntityId = target.handle.id;
                        context.targetPosition = target.worldTransform.position;
                        targetJson = nlohmann::json::object();
                        targetJson["id"] = entityId(target.handle);
                        targetJson["name"] = target.name;
                        targetJson["position"] = env::vec3Json(context.targetPosition);
                    }
                    else if (value.is_array() || value.is_object())
                    {
                        const glm::vec3 point = ArgReader::toVec3(value, "target");
                        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
                        {
                            throw ArgError("argument 'target' must hold finite numbers");
                        }
                        context.hasTarget = true;
                        context.targetPosition = point;
                        targetJson = nlohmann::json::object();
                        targetJson["position"] = env::vec3Json(point);
                    }
                    else
                    {
                        throw ArgError("argument 'target' must be an entity id or a world point [x, y, z]");
                    }
                }

                auto& dispatcher = events::EventDispatcher::instance();
                const bool playing = dispatcher.query(events::editor::IsPlayModeQuery{});
                const services::EQSQueryInfo info = queryInfoOnMain(name);
                if (!info.exists)
                {
                    throw std::runtime_error(std::format("There is no EQS query '{}'; eqs_list_queries lists the "
                                                         "templates and eqs_set_query creates one", name));
                }
                if (!info.valid)
                {
                    throw std::runtime_error(std::format("The EQS query '{}' is invalid, so it cannot run:\n{}\nFix it "
                                                         "with eqs_set_query (eqs_get_query shows the file).",
                                                         name, eqstool::specErrorLines(info.errors)));
                }
                const std::optional<eqs::EQSQuerySpec> fileSpec = readTemplateSpecOnMain(info.path);

                events::ai::RunEQSQueryNowQuery runQuery;
                runQuery.queryName = name;
                runQuery.context = context;
                const eqs::EQSDebugResult run = dispatcher.query(runQuery);

                if (show)
                {
                    // Replaces the previous run's drawing, also with nothing for a run without candidates.
                    events::ai::ShowEQSDebugCommand overlay;
                    overlay.result = run;
                    overlay.seconds = static_cast<float>(showSeconds);
                    dispatcher.execute(overlay);
                }

                const std::vector<eqstool::RunTest> tests =
                    eqstool::runTests(run.testNames, fileSpec.has_value() ? &*fileSpec : nullptr);
                nlohmann::json out = eqstool::runReplyJson(run, tests, maxCandidates, [](uint64_t id) -> nlohmann::json
                {
                    services::EntityHandle handle;
                    handle.id = id;
                    const std::optional<services::EntityData> entity = findEntity(handle);
                    nlohmann::json entry = nlohmann::json::object();
                    entry["id"] = entityId(handle);
                    entry["name"] = entity.has_value() ? nlohmann::json(entity->name) : nlohmann::json(nullptr);
                    return entry;
                });

                const auto hasTest = [&run](std::string_view type)
                {
                    return std::find(run.testNames.begin(), run.testNames.end(), type) != run.testNames.end();
                };
                nlohmann::json& notes = out["notes"];
                if (!playing && hasTest("Trace"))
                {
                    notes.push_back("Edit mode: physics has no entity colliders outside Play (their bodies are created "
                                    "when Play starts), so Trace tests see the terrain only; run the query in Play for "
                                    "the real line of sight.");
                }
                if (!agent.has_value() && hasTest("PathCost"))
                {
                    notes.push_back(std::format("The querier has no NavmeshAgent, so PathCost searched paths for the "
                                                "default agent size (radius {}, height {}).",
                                                context.agentRadius, context.agentHeight));
                }
                if (run.result.status == eqs::EQSQueryStatus::Failed && !context.hasTarget &&
                    run.result.error.find("needs a target") != std::string::npos)
                {
                    notes.push_back("This template uses the target: pass target (an entity id or a world point "
                                    "[x, y, z]).");
                }
                if (show && !run.candidates.empty())
                {
                    notes.push_back(std::format("The candidates are drawn in the viewport for {} s (kept ones red to "
                                                "green by total, dropped ones small and gray, the best one a large "
                                                "cyan sphere); viewport_screenshot shows them.", showSeconds));
                }

                nlohmann::json querierJson = nlohmann::json::object();
                querierJson["id"] = entityId(querier.handle);
                querierJson["name"] = querier.name;
                querierJson["position"] = env::vec3Json(context.querierPosition);
                querierJson["forward"] = env::vec3Json(context.querierForward);
                nlohmann::json contextJson = nlohmann::json::object();
                contextJson["querier"] = std::move(querierJson);
                contextJson["target"] = std::move(targetJson);
                contextJson["agentRadius"] = env::floatJson(context.agentRadius);
                contextJson["agentHeight"] = env::floatJson(context.agentHeight);
                out["name"] = name;
                out["context"] = std::move(contextJson);

                std::string summary = runSummary(name, run, out);
                ToolResult result = reply(std::move(out), std::move(summary));
                // A run that could not finish (a missing target, an engine provider missing, a throw) is in-band.
                result.isError = run.result.status != eqs::EQSQueryStatus::Completed;
                return result;
            };
            registry.add(std::move(tool));
        }
    }

    void registerEQSTools(ToolRegistry& registry, const ToolContext& context)
    {
        registerEQSListQueries(registry, context);
        registerEQSGetQuery(registry);
        registerEQSSetQuery(registry);
        registerEQSRun(registry);
    }
}

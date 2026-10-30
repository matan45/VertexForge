#include "CoreTools.hpp"
#include "BehaviorTreeToolSupport.hpp"
#include "ContentHelpers.hpp"
#include "PathSandbox.hpp"
#include "SettingsTable.hpp"
#include "ToolHelpers.hpp"
#include "../protocol/ArgReader.hpp"

#include "events/EventDispatcher.hpp"
#include "events/ai/BehaviorTreeEvents.hpp"
#include "events/ai/EQSEvents.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// VK-1660 behavior-tree tools: bt_list_node_types, bt_create, bt_get, bt_set, bt_edit and bt_debug_state (the
// blackboard and assignment tools are in BehaviorTreeBlackboardTools.cpp). Trees are exchanged as the nested JSON of
// vf://docs/bt section 3 and written only through SaveBehaviorTreeAssetCommand (content-hash guard, strict catalogue
// + engine validation, atomic write, AssetSavedNotification, hot reload of running trees). Asset writes are allowed in
// Play mode and are not undoable; bt_debug_state reads the live runtime without taking over the editor's BT debugger.
namespace mcp::tools
{
    namespace
    {
        namespace bt = behaviortree;
        namespace fs = std::filesystem;
        using bt::catalog::NodeCategory;
        using bt::catalog::NodeSpec;
        using bt::catalog::ParamKind;
        using bt::catalog::ParamSpec;

        constexpr std::chrono::milliseconds authoringTimeout{15000};
        constexpr std::chrono::milliseconds workerTimeout{15000};
        // McpServer does not enforce a Worker tool's timeout, so each keeps its own deadline below it.
        constexpr std::chrono::milliseconds listDeadline{12000};
        constexpr std::chrono::milliseconds sampleDeadline{13000};
        constexpr std::size_t maxListedScripts = 200;

        constexpr const char* treeFormatHelp =
            "Tree JSON (vf://docs/bt section 3): {name, blackboard?: [{name, type: Float|Int|Bool|String|Vec3|Entity, "
            "default?}], root: node}; node = {type, name?, id?, params?: {...}, children?: [node] | child: node, "
            "mappings? (DynamicSubTree)}. The Root is implicit; composites take 1+ children, decorators and services "
            "exactly 1, tasks none. ScriptTask params {script: \"game/ai/<Name>.mt\"}; enums are case-insensitive.";

        nlohmann::json pathSchema()
        {
            return schema::string(".vfBehaviorTree path relative to the project's asset folder, e.g. "
                                  "\"ai/EnemyAI.vfBehaviorTree\"");
        }

        nlohmann::json hashSchema()
        {
            return schema::string("contentHash from bt_get / bt_create / bt_set / bt_edit: refuse the write when the "
                                  "file changed since");
        }

        void appendEngineWarnings(std::vector<std::string>& warnings,
                                  const std::vector<bt::validation::Diagnostic>& diagnostics)
        {
            for (std::string& text : bttool::diagnosticTexts(diagnostics, false))
            {
                warnings.push_back(std::move(text));
            }
        }

        std::string countText(std::size_t count, const char* noun)
        {
            return std::to_string(count) + " " + noun + (count == 1 ? "" : "s");
        }

        // ------------------------------------------------------------------
        // bt_list_node_types
        // ------------------------------------------------------------------

        std::string childRuleShort(const NodeSpec& spec)
        {
            if (spec.maxChildren == 0)
            {
                return "none";
            }
            if (spec.minChildren == spec.maxChildren)
            {
                return "exactly " + std::to_string(spec.minChildren);
            }
            if (spec.maxChildren == bt::catalog::kUnlimitedChildren)
            {
                return std::to_string(spec.minChildren) + " or more";
            }
            return std::to_string(spec.minChildren) + "-" + std::to_string(spec.maxChildren);
        }

        // Any thread: the catalogue only (no Entity defaults exist, so valueJson never dispatches here).
        nlohmann::json paramEntry(const ParamSpec& param)
        {
            nlohmann::json out{{"name", param.name},
                               {"kind", bt::catalog::paramKindToString(param.kind)},
                               {"required", param.required}};
            if (!param.required && param.kind != ParamKind::ScriptPath)
            {
                out["default"] = bttool::valueJson(param.defaultValue);
            }
            if (param.hasRange)
            {
                if (param.kind == ParamKind::Int)
                {
                    out["min"] = static_cast<int64_t>(param.minValue);
                    out["max"] = static_cast<int64_t>(param.maxValue);
                }
                else
                {
                    out["min"] = env::floatJson(param.minValue);
                    out["max"] = env::floatJson(param.maxValue);
                }
            }
            if (!param.enumValues.empty())
            {
                out["values"] = param.enumValues;
            }
            if (!param.keyTypes.empty())
            {
                nlohmann::json types = nlohmann::json::array();
                for (const bt::BlackboardValueType type : param.keyTypes)
                {
                    types.push_back(bt::blackboardValueTypeToString(type));
                }
                out["keyTypes"] = std::move(types);
                if (param.allowEmptyKey)
                {
                    out["emptyAllowed"] = true;
                }
            }
            if (!param.valueKeyParam.empty())
            {
                out["typeOfKey"] = param.valueKeyParam;
            }
            if (!param.serviceVariant.empty())
            {
                out["serviceType"] = param.serviceVariant;
            }
            out["description"] = param.description;
            return out;
        }

        nlohmann::json nodeTypeEntry(const NodeSpec& spec)
        {
            nlohmann::json params = nlohmann::json::array();
            for (const ParamSpec& param : spec.params)
            {
                params.push_back(paramEntry(param));
            }
            nlohmann::json out{{"type", spec.name},
                               {"category", bt::catalog::categoryToString(spec.category)},
                               {"children", childRuleShort(spec)},
                               {"params", std::move(params)},
                               {"description", spec.description}};
            if (spec.category == NodeCategory::Root)
            {
                out["implicit"] = true;
            }
            return out;
        }

        void registerListNodeTypes(ToolRegistry& registry, const ToolContext& context)
        {
            ToolDef tool;
            tool.name = "bt_list_node_types";
            tool.title = "List behavior tree node types";
            tool.description =
                "The behavior tree node catalogue: every node type with its category (composite, decorator, service, "
                "task), child rule and parameters - kind, required, default, range (min/max), enum values, the "
                "blackboard key types a key parameter accepts (keyTypes), the key whose type a value follows "
                "(typeOfKey) and, for a Service, the serviceType a parameter belongs to. Also: serviceTypes, "
                "blackboardTypes, eqsQueries (the project's EQS templates, for EnvironmentQuery / EQSRefresh "
                "queryName) and scriptTasks (game/**/*.mt scripts whose first @Script class declares "
                "tick(float): string, with their class, optional hooks and leading comment - the scripts a ScriptTask "
                "can run). 'type' limits the nodes to one type. vf://docs/bt explains the tree JSON and the patterns. "
                "Works in any mode.";
            tool.inputSchema = closedObject({
                {"type", schema::string("Only this node type, e.g. \"MoveTo\" (case-insensitive)")},
                {"includeScripts", schema::boolean("List the project's ScriptTask scripts. Default true.")}
            });
            tool.affinity = ThreadAffinity::Worker;
            tool.timeout = workerTimeout;
            tool.readOnly = true;
            tool.handler = [context](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"type", "includeScripts"}, "bt_list_node_types");
                const ArgReader reader(args);
                const auto deadline = std::chrono::steady_clock::now() + listDeadline;
                const bool includeScripts = reader.optBool("includeScripts", true);

                nlohmann::json nodes = nlohmann::json::array();
                if (reader.has("type"))
                {
                    const std::string wanted = reader.requireString("type");
                    const NodeSpec* spec = bt::catalog::findByName(wanted);
                    if (spec == nullptr)
                    {
                        std::string names;
                        for (const NodeSpec& candidate : bt::catalog::nodes())
                        {
                            names += (names.empty() ? "" : ", ") + candidate.name;
                        }
                        throw ArgError("argument 'type': unknown node type '" + wanted + "'; types: " + names);
                    }
                    nodes.push_back(nodeTypeEntry(*spec));
                }
                else
                {
                    for (const NodeSpec& spec : bt::catalog::nodes())
                    {
                        nodes.push_back(nodeTypeEntry(spec));
                    }
                }

                std::vector<std::string> notes;
                nlohmann::json eqsQueries = context.runOnMain([]() -> nlohmann::json
                {
                    try
                    {
                        nlohmann::json list = nlohmann::json::array();
                        for (const services::EQSQueryInfo& info :
                             events::EventDispatcher::instance().query(events::ai::ListEQSQueriesQuery{}))
                        {
                            nlohmann::json entry{{"name", info.name}, {"valid", info.valid},
                                                 {"description", info.description}, {"summary", info.summary}};
                            if (!info.errors.empty())
                            {
                                const eqs::EQSSpecError& first = info.errors.front();
                                entry["error"] = first.path.empty() ? first.message : first.path + ": " + first.message;
                            }
                            list.push_back(std::move(entry));
                        }
                        return list;
                    }
                    catch (const std::runtime_error&)
                    {
                        return nullptr;
                    }
                });
                if (eqsQueries.is_null())
                {
                    eqsQueries = nlohmann::json::array();
                    notes.emplace_back("EQS is not available, so no query templates are listed");
                }

                nlohmann::json scriptTasks = nlohmann::json::array();
                bool truncated = false;
                if (includeScripts)
                {
                    const nlohmann::json rootJson = context.runOnMain([]() -> nlohmann::json
                    {
                        try
                        {
                            return pathToUtf8(scriptsRootOnMain());
                        }
                        catch (const std::runtime_error&)
                        {
                            return nullptr;
                        }
                    });
                    if (!rootJson.is_string())
                    {
                        notes.emplace_back("no project is loaded, so no ScriptTask scripts are listed");
                    }
                    else
                    {
                        const fs::path root = pathFromUtf8(rootJson.get<std::string>());
                        for (const std::string& path : listGameScripts(root, maxListedScripts, &truncated))
                        {
                            if (std::chrono::steady_clock::now() >= deadline)
                            {
                                truncated = true;
                                break;
                            }
                            ScriptReadResult read;
                            try
                            {
                                read = readScript(root, path);
                            }
                            catch (const std::exception&)
                            {
                                continue;
                            }
                            if (read.status != ScriptReadStatus::Ok)
                            {
                                continue;
                            }
                            const std::optional<bt::ScriptTaskInfo> info = bt::scanScriptTask(read.source);
                            if (!info.has_value())
                            {
                                continue;
                            }
                            scriptTasks.push_back({{"path", read.path},
                                                   {"class", info->className},
                                                   {"hooks", {{"onStart", info->hasOnStart},
                                                              {"onAbort", info->hasOnAbort},
                                                              {"onEnd", info->hasOnEnd}}},
                                                   {"summary", info->summary}});
                        }
                        notes.emplace_back("a ScriptTask runs its class through tick(float deltaTime): string "
                                           "(\"running\", \"success\", anything else = failure) - write one with "
                                           "script_write and build it with scripts_build (section 12 of "
                                           "vf://docs/mtype-api)");
                    }
                }

                const std::size_t nodeCount = nodes.size();
                const std::size_t scriptCount = scriptTasks.size();
                nlohmann::json out{{"nodes", std::move(nodes)},
                                   {"serviceTypes", bt::catalog::serviceTypes()},
                                   {"blackboardTypes", bttool::blackboardTypeNames()},
                                   {"eqsQueries", std::move(eqsQueries)},
                                   {"format", "vf://docs/bt"}};
                if (includeScripts)
                {
                    out["scriptTasks"] = std::move(scriptTasks);
                    if (truncated)
                    {
                        out["scriptTasksTruncated"] = true;
                    }
                }
                out["notes"] = notes;
                return reply(std::move(out), countText(nodeCount, "node type") +
                                             (includeScripts ? ", " + countText(scriptCount, "ScriptTask script")
                                                             : std::string()) + ".");
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // bt_create / bt_get / bt_set / bt_edit
        // ------------------------------------------------------------------

        void registerCreate(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "bt_create";
            tool.title = "Create behavior tree";
            tool.description =
                std::string("Create a new .vfBehaviorTree from tree JSON. ") + treeFormatHelp +
                " Strict: an unknown type or parameter, a missing required parameter, a wrong child count, an "
                "undeclared key or one of the wrong type, a number out of range, a script that is not a ScriptTask, a "
                "missing SubTree file or an unknown EQS query is refused (up to 20 problems, with JSON paths like "
                "root.child.children[1].params.compareValue), and nothing is written. Every optional parameter is "
                "written explicitly; a given id is kept, the others are assigned in pre-order; editor positions are "
                "laid out as a tidy tree. Refused when the file exists (bt_set replaces a tree, bt_edit changes one). "
                "Allowed in Play mode. Not undoable. Returns {path, contentHash, nodes [{id, name, type}], keys, "
                "warnings}; assign it with bt_assign.";
            tool.inputSchema = closedObject({
                {"path", pathSchema()},
                {"tree", schema::anyObject("The tree: {name, blackboard?, root}")}
            }, {"path", "tree"});
            tool.timeout = authoringTimeout;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"path", "tree"}, "bt_create");
                const ArgReader reader(args);
                const bttool::TreeFile file =
                    bttool::resolveTreeArgument(reader.requireString("path"), "argument 'path'", false);
                std::error_code ec;
                if (fs::exists(file.file, ec))
                {
                    return ToolResult::error("'" + file.relative + "' already exists: bt_set replaces a tree and bt_edit "
                                             "changes one (bt_get reads it first)");
                }

                bttool::SpecOptions options;
                options.selfEnginePath = file.enginePath;
                bttool::ParsedTree parsed = bttool::parseTreeSpec(reader.raw("tree"), options);
                const services::BehaviorTreeAssetSaveResult result =
                    bttool::saveTree(file, parsed.data, std::nullopt, true, std::nullopt);
                if (!result.success)
                {
                    return bttool::saveFailure(file, result);
                }

                std::vector<std::string> warnings = std::move(parsed.warnings);
                appendEngineWarnings(warnings, result.diagnostics);
                nlohmann::json nodes = bttool::nodeList(parsed.data.graph);
                const std::string summary = "Created " + file.relative + ": " + countText(nodes.size(), "node") + ", " +
                                            countText(parsed.data.graph.blackboardKeys.size(), "blackboard key") + ".";
                return reply({{"path", file.relative},
                              {"contentHash", bttool::hashHex(result.contentHash)},
                              {"nodes", std::move(nodes)},
                              {"keys", bttool::keySpecsJson(parsed.data.graph.blackboardKeys)},
                              {"warnings", warnings}},
                             summary);
            };
            registry.add(std::move(tool));
        }

        void registerGet(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "bt_get";
            tool.title = "Get behavior tree";
            tool.description =
                "Read a behavior tree as the tree JSON bt_create and bt_set take, with every node's id (editor "
                "positions are left out; bt_set keeps them). 'path' names the file, or 'entity' reads the tree of its "
                "BehaviorTree component. Returns {path, contentHash (bt_set / bt_edit expectedContentHash), tree, "
                "validation {errors, warnings, strict (findings that block an edit touching those nodes)}, attachedTo "
                "[{entity, name, component, enabled, live}], legacyParams? (stored parameters the catalogue does not "
                "know: bt_set drops them), unreachable?, warnings}. Works in any mode.";
            tool.inputSchema = closedObject({
                {"path", pathSchema()},
                {"entity", schema::entity("Read the tree this entity's BehaviorTree component runs instead")}
            });
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"path", "entity"}, "bt_get");
                const ArgReader reader(args);
                const bttool::TreeFile file = bttool::treeFromArguments(reader, "bt_get");
                const services::BehaviorTreeAssetLoadResult loaded = bttool::loadTree(file.enginePath);
                if (!loaded.success)
                {
                    return ToolResult::error("Cannot read '" + file.relative + "': " + loaded.error);
                }

                bttool::EmittedTree emitted = bttool::emitTree(loaded.data);
                const std::size_t nodeCount = bttool::nodeList(loaded.data.graph).size();
                nlohmann::json out{{"path", file.relative},
                                   {"contentHash", bttool::hashHex(loaded.contentHash)},
                                   {"tree", std::move(emitted.tree)},
                                   {"validation", bttool::validationJson(loaded.data)},
                                   {"attachedTo", bttool::attachedTo(file.enginePath)}};
                if (reader.has("entity"))
                {
                    out["entity"] = reader.requireEntity("entity");
                }
                if (!emitted.legacyParams.empty())
                {
                    out["legacyParams"] = std::move(emitted.legacyParams);
                }
                if (!emitted.unreachable.empty())
                {
                    out["unreachable"] = std::move(emitted.unreachable);
                }
                out["warnings"] = emitted.warnings;
                return reply(std::move(out), file.relative + ": '" + loaded.data.name + "', " +
                                             countText(nodeCount, "node") + ".");
            };
            registry.add(std::move(tool));
        }

        // The file must exist and, with expectedContentHash, still hash as the agent read it.
        services::BehaviorTreeAssetLoadResult loadForWrite(const bttool::TreeFile& file, const ArgReader& reader)
        {
            services::BehaviorTreeAssetLoadResult loaded = bttool::loadTree(file.enginePath);
            if (!loaded.success)
            {
                throw std::runtime_error("Cannot read '" + file.relative + "': " + loaded.error);
            }
            const std::optional<uint64_t> expected = bttool::optHash(reader, "expectedContentHash");
            if (expected.has_value() && *expected != loaded.contentHash)
            {
                throw std::runtime_error("'" + file.relative + "' changed on disk since it was read (its contentHash is "
                                         "now " + bttool::hashHex(loaded.contentHash) + "); bt_get it again");
            }
            return loaded;
        }

        void registerSet(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "bt_set";
            tool.title = "Replace behavior tree";
            tool.description =
                std::string("Replace a whole behavior tree with tree JSON (bt_get's 'tree' passes back unchanged). ") +
                treeFormatHelp +
                " Nodes that keep their id keep their editor position; nodes without an id get new ids. relayout:true "
                "lays every node out again. Same strict checks as bt_create; nothing is written on error. Allowed in "
                "Play mode: running trees that use the file reload before their next tick. Not undoable. Returns "
                "{path, changed, contentHash, reloadedRuntimes, nodes, warnings}.";
            tool.inputSchema = closedObject({
                {"path", pathSchema()},
                {"tree", schema::anyObject("The whole tree: {name, blackboard?, root}")},
                {"expectedContentHash", hashSchema()},
                {"relayout", schema::boolean("Lay every node out again as a tidy tree. Default false.")}
            }, {"path", "tree"});
            tool.timeout = authoringTimeout;
            tool.destructive = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"path", "tree", "expectedContentHash", "relayout"}, "bt_set");
                const ArgReader reader(args);
                const bttool::TreeFile file =
                    bttool::resolveTreeArgument(reader.requireString("path"), "argument 'path'", true);
                const services::BehaviorTreeAssetLoadResult loaded = loadForWrite(file, reader);

                bttool::SpecOptions options;
                options.selfEnginePath = file.enginePath;
                options.previous = &loaded.data.graph;
                options.relayout = reader.optBool("relayout", false);
                bttool::ParsedTree parsed = bttool::parseTreeSpec(reader.raw("tree"), options);
                nlohmann::json nodes = bttool::nodeList(parsed.data.graph);

                if (bttool::sameTreeContent(parsed.data, loaded.data))
                {
                    return reply({{"path", file.relative}, {"changed", false},
                                  {"contentHash", bttool::hashHex(loaded.contentHash)}, {"reloadedRuntimes", 0},
                                  {"nodes", std::move(nodes)}, {"warnings", parsed.warnings}},
                                 file.relative + " is unchanged.");
                }
                const services::BehaviorTreeAssetSaveResult result =
                    bttool::saveTree(file, parsed.data, loaded.contentHash, false, std::nullopt);
                if (!result.success)
                {
                    return bttool::saveFailure(file, result);
                }
                std::vector<std::string> warnings = std::move(parsed.warnings);
                appendEngineWarnings(warnings, result.diagnostics);
                const int reloaded = bttool::liveRuntimesUsing(file.enginePath);
                return reply({{"path", file.relative}, {"changed", true},
                              {"contentHash", bttool::hashHex(result.contentHash)}, {"reloadedRuntimes", reloaded},
                              {"nodes", std::move(nodes)}, {"warnings", warnings}},
                             "Replaced " + file.relative +
                                 (reloaded > 0 ? "; " + countText(static_cast<std::size_t>(reloaded), "running tree") +
                                                     " reload"
                                               : std::string()) + ".");
            };
            registry.add(std::move(tool));
        }

        void registerEdit(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "bt_edit";
            tool.title = "Edit behavior tree";
            tool.description =
                "Change a behavior tree with 1-64 operations, applied in order to a copy that is checked once and "
                "saved once (nothing is written on error). node / parent = a node id, or a name only one node has (an "
                "ambiguous name lists the ids). Ops: {op:\"setParams\", node, params} merges parameters (null resets "
                "an optional one; covers abortMode, compareValue, a Service's serviceType and a ScriptTask's script; "
                "'mappings' replaces a DynamicSubTree's); {op:\"rename\", node, name}; {op:\"addChild\", parent, "
                "index?, node: <node JSON>}; {op:\"remove\", node} (with everything below it); {op:\"move\", node, "
                "parent, index?}; {op:\"wrap\", node, with: <decorator / service / composite JSON without children>} "
                "puts a node above it; {op:\"unwrap\", node} replaces a node that has one child by that child; "
                "{op:\"replace\", node, with: <node JSON>} (without children the new node takes the old one's; it "
                "keeps the old id and position unless it gives its own). Node JSON is the bt_create format. The "
                "strict checks apply to the nodes the ops create or change and to the parents whose children change; "
                "untouched nodes keep what they store (legacy parameters included). Ids never change. Allowed in Play "
                "mode (running trees reload). Not undoable. Returns {path, changed, touched [{id, name, type}], "
                "contentHash, reloadedRuntimes, warnings}.";
            tool.inputSchema = closedObject({
                {"path", pathSchema()},
                {"ops", schema::array(schema::anyObject("{op, ...}"), "The operations, applied in order")},
                {"expectedContentHash", hashSchema()},
                {"relayout", schema::boolean("Lay every node out again as a tidy tree. Default false (only new "
                                             "nodes are placed).")}
            }, {"path", "ops"});
            tool.timeout = authoringTimeout;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"path", "ops", "expectedContentHash", "relayout"}, "bt_edit");
                const ArgReader reader(args);
                const bttool::TreeFile file =
                    bttool::resolveTreeArgument(reader.requireString("path"), "argument 'path'", true);
                const services::BehaviorTreeAssetLoadResult loaded = loadForWrite(file, reader);

                bttool::SpecOptions options;
                options.selfEnginePath = file.enginePath;
                options.relayout = reader.optBool("relayout", false);
                bttool::EditOutcome edit = bttool::applyEdits(loaded.data, reader.raw("ops"), options);

                std::vector<uint32_t> ids(edit.touched.begin(), edit.touched.end());
                std::sort(ids.begin(), ids.end());
                nlohmann::json touched = nlohmann::json::array();
                for (const uint32_t id : ids)
                {
                    const bt::BTNode* node = edit.data.graph.findNodeById(id);
                    if (node != nullptr && id != edit.data.graph.rootNodeId)
                    {
                        touched.push_back(bttool::nodeSummary(*node));
                    }
                }

                if (bttool::sameTreeContent(edit.data, loaded.data))
                {
                    return reply({{"path", file.relative}, {"changed", false}, {"touched", std::move(touched)},
                                  {"contentHash", bttool::hashHex(loaded.contentHash)}, {"reloadedRuntimes", 0},
                                  {"warnings", edit.warnings}},
                                 file.relative + " is unchanged.");
                }
                const services::BehaviorTreeAssetSaveResult result =
                    bttool::saveTree(file, edit.data, loaded.contentHash, false, edit.touched);
                if (!result.success)
                {
                    return bttool::saveFailure(file, result);
                }
                std::vector<std::string> warnings = std::move(edit.warnings);
                appendEngineWarnings(warnings, result.diagnostics);
                const int reloaded = bttool::liveRuntimesUsing(file.enginePath);
                const std::size_t touchedCount = touched.size();
                return reply({{"path", file.relative}, {"changed", true}, {"touched", std::move(touched)},
                              {"contentHash", bttool::hashHex(result.contentHash)}, {"reloadedRuntimes", reloaded},
                              {"warnings", warnings}},
                             "Edited " + file.relative + ": " + countText(touchedCount, "node") + " touched.");
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // bt_debug_state
        // ------------------------------------------------------------------

        struct DebugNode
        {
            std::string name;
            std::string type;
            uint32_t subtreeNode = 0;  // != 0: spliced in from the static SubTree node with this id
        };

        // The expanded tree's nodes; those under an entry of subtreeEntryMap came from a static SubTree.
        std::unordered_map<uint32_t, DebugNode> debugNodes(const bt::BTRuntimeState& state)
        {
            std::unordered_map<uint32_t, DebugNode> nodes;
            if (!state.tree)
            {
                return nodes;
            }
            const bt::BTGraph& graph = state.tree->graph;
            for (const bt::BTNode& node : graph.nodes)
            {
                const std::string type = bt::nodeTypeToString(node.type);
                nodes[node.id] = DebugNode{node.name.empty() ? type : node.name, type, 0};
            }
            for (const auto& [authored, entry] : state.snapshot.subtreeEntryMap)
            {
                std::vector<uint32_t> pending{entry};
                std::unordered_set<uint32_t> seen;
                while (!pending.empty())
                {
                    const uint32_t id = pending.back();
                    pending.pop_back();
                    if (!seen.insert(id).second)
                    {
                        continue;
                    }
                    if (auto it = nodes.find(id); it != nodes.end())
                    {
                        it->second.subtreeNode = authored;
                    }
                    for (const uint32_t child : bttool::childIds(graph, id))
                    {
                        pending.push_back(child);
                    }
                }
            }
            return nodes;
        }

        nlohmann::json debugNodeJson(uint32_t id, const std::unordered_map<uint32_t, DebugNode>& nodes)
        {
            nlohmann::json out{{"id", id}};
            const auto it = nodes.find(id);
            if (it == nodes.end())
            {
                out["name"] = nullptr;
                out["type"] = nullptr;
                return out;
            }
            out["name"] = it->second.name;
            out["type"] = it->second.type;
            if (it->second.subtreeNode != 0)
            {
                out["subtree"] = true;
                out["subtreeNode"] = it->second.subtreeNode;
            }
            return out;
        }

        std::string notAttachedNote(bool play, const std::optional<services::BehaviorTreeComponentData>& component)
        {
            if (!play)
            {
                return "Behavior trees run only in Play mode: play_start, then call bt_debug_state again (sampleSeconds "
                       "watches it for up to 10 s).";
            }
            if (!component.has_value())
            {
                return "The entity has no BehaviorTree component (bt_assign it in Edit mode; trees attach when Play "
                       "starts).";
            }
            if (!component->enabled)
            {
                return "Its BehaviorTree component is disabled.";
            }
            if (component->treePath.empty())
            {
                return "Its BehaviorTree component has no tree.";
            }
            return "Its tree did not attach when Play started (logs_read shows why); a tree assigned during Play "
                   "attaches at the next play_start.";
        }

        // Main thread. full: the whole state; else one compact sample {tick, status, leaf, path}.
        nlohmann::json debugStateOnMain(uint32_t id, bool full)
        {
            const services::EntityHandle entity = toHandle(id);
            const std::optional<services::EntityData> data = findEntity(entity);
            if (!data.has_value())
            {
                throw std::runtime_error("Entity " + std::to_string(id) +
                                         " does not exist; use entity_find or scene_get_hierarchy");
            }
            const bool play = bttool::playMode();
            bt::BTRuntimeState state;
            try
            {
                events::ai::GetBehaviorTreeRuntimeStateQuery query;
                query.entity = entity;
                state = events::EventDispatcher::instance().query(query);
            }
            catch (const std::runtime_error&)
            {
                // No behavior tree service: nothing runs.
            }

            nlohmann::json out{{"entity", id}, {"name", data->name}, {"mode", play ? "play" : "edit"},
                               {"attached", state.attached}};
            if (!state.attached)
            {
                if (full)
                {
                    std::optional<services::BehaviorTreeComponentData> component;
                    try
                    {
                        component = bttool::componentData(entity);
                    }
                    catch (const std::runtime_error&)
                    {
                    }
                    out["component"] = component.has_value()
                        ? nlohmann::json{{"tree", bttool::reportTreePath(component->treePath)},
                                         {"enabled", component->enabled}}
                        : nlohmann::json(nullptr);
                    out["note"] = notAttachedNote(play, component);
                }
                return out;
            }

            const std::unordered_map<uint32_t, DebugNode> nodes = debugNodes(state);
            const uint32_t rootId = state.tree ? state.tree->graph.rootNodeId : 0;
            std::vector<uint32_t> path = state.snapshot.activePath;
            if (!path.empty() && path.front() == rootId)
            {
                path.erase(path.begin());  // the Root is implicit in the tree JSON
            }
            out["status"] = state.status;
            out["tick"] = state.tickCount;
            const nlohmann::json leaf = path.empty() ? nlohmann::json(nullptr) : debugNodeJson(path.back(), nodes);
            if (!full)
            {
                std::string names;
                for (const uint32_t node : path)
                {
                    const auto it = nodes.find(node);
                    names += (names.empty() ? "" : " > ") + (it != nodes.end() ? it->second.name : std::to_string(node));
                }
                out["leaf"] = leaf;
                out["path"] = names;
                return out;
            }

            nlohmann::json activePath = nlohmann::json::array();
            for (const uint32_t node : path)
            {
                activePath.push_back(debugNodeJson(node, nodes));
            }
            nlohmann::json running = nlohmann::json::array();
            if (state.tree)
            {
                // Pre-order over the tree the runtime ticks, so parents come before their children.
                std::vector<uint32_t> pending;
                const std::vector<uint32_t> top = bttool::childIds(state.tree->graph, rootId);
                pending.assign(top.rbegin(), top.rend());
                std::unordered_set<uint32_t> seen{rootId};
                while (!pending.empty())
                {
                    const uint32_t node = pending.back();
                    pending.pop_back();
                    if (!seen.insert(node).second)
                    {
                        continue;
                    }
                    const auto status = state.snapshot.nodeStatuses.find(node);
                    if (status != state.snapshot.nodeStatuses.end() && status->second == bt::BTNodeStatus::Running)
                    {
                        nlohmann::json entry = debugNodeJson(node, nodes);
                        const auto elapsed = state.snapshot.elapsedTimes.find(node);
                        entry["elapsed"] = env::floatJson(elapsed != state.snapshot.elapsedTimes.end() ? elapsed->second : 0.0f);
                        running.push_back(std::move(entry));
                    }
                    const std::vector<uint32_t> kids = bttool::childIds(state.tree->graph, node);
                    pending.insert(pending.end(), kids.rbegin(), kids.rend());
                }
            }
            nlohmann::json blackboard = nlohmann::json::object();
            for (const auto& [key, value] : state.snapshot.blackboard)
            {
                blackboard[key] = bttool::valueJson(value);
            }

            out["tree"] = bttool::reportTreePath(state.treePath);
            out["enabled"] = state.enabled;
            out["activePath"] = std::move(activePath);
            out["activeLeaf"] = leaf;
            out["running"] = std::move(running);
            out["blackboard"] = std::move(blackboard);
            if (state.snapshot.paused)
            {
                out["paused"] = true;
                out["note"] = "The editor's BT debugger has paused this entity's tree.";
            }
            return out;
        }

        // The running leaf's name in a sample ("leaf") or a full state ("activeLeaf"); null when there is none.
        nlohmann::json leafName(const nlohmann::json& sample, const char* key = "leaf")
        {
            const auto it = sample.find(key);
            return it != sample.end() && it->is_object() ? it->value("name", nlohmann::json(nullptr))
                                                         : nlohmann::json(nullptr);
        }

        nlohmann::json leafId(const nlohmann::json& sample)
        {
            const auto it = sample.find("leaf");
            return it != sample.end() && it->is_object() ? (*it)["id"] : nlohmann::json(nullptr);
        }

        void registerDebugState(ToolRegistry& registry, const ToolContext& context)
        {
            ToolDef tool;
            tool.name = "bt_debug_state";
            tool.title = "Behavior tree debug state";
            tool.description =
                "The live behavior tree of an entity in Play mode: {attached, tree, status (running | success | failure "
                "| stopped), enabled, tick, activePath [{id, name, type}] from the top node down to the running leaf, "
                "activeLeaf, running [{id, name, type, elapsed}], blackboard {key: value} (Entity values as {entity, "
                "name})}. Ids are the tree's (bt_get); a node spliced in from a static SubTree carries subtree:true and "
                "subtreeNode (the SubTree node's id). sampleSeconds (0-10) keeps watching every sampleIntervalMs "
                "(50-1000, default 100) and adds samples [{t, tick, status, leaf, path}] and switches [{t, from, to}] "
                "(the running leaf changing), e.g. to see Patrol -> Chase -> Attack. It never takes over the editor's "
                "BT debugger. In Edit mode no tree runs: attached:false with a note. Works in any mode.";
            tool.inputSchema = closedObject({
                {"entity", schema::entity("Entity whose behavior tree to read")},
                {"sampleSeconds", schema::numberRange("Keep sampling this long (seconds). Default 0 = one read.", 0, 10)},
                {"sampleIntervalMs", schema::integerRange("Milliseconds between samples. Default 100.", 50, 1000)}
            }, {"entity"});
            tool.affinity = ThreadAffinity::Worker;
            tool.timeout = workerTimeout;
            tool.readOnly = true;
            tool.handler = [context](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"entity", "sampleSeconds", "sampleIntervalMs"}, "bt_debug_state");
                const ArgReader reader(args);
                const uint32_t id = reader.requireEntity("entity");
                const double seconds = reader.optNumberInRange("sampleSeconds", 0.0, 10.0, 0.0);
                const int64_t intervalMs = reader.optIntInRange("sampleIntervalMs", 50, 1000, 100);

                const auto start = std::chrono::steady_clock::now();
                const auto deadline = start + sampleDeadline;
                nlohmann::json state = context.runOnMain([id]() { return debugStateOnMain(id, true); });
                if (!state.value("attached", false) || seconds <= 0.0)
                {
                    const nlohmann::json leaf = leafName(state, "activeLeaf");
                    const std::string summary = state.value("attached", false)
                        ? "'" + state["name"].get<std::string>() + "': " + state.value("status", std::string()) +
                          (leaf.is_string() ? ", running '" + leaf.get<std::string>() + "'" : std::string()) + "."
                        : "No behavior tree runs on '" + state["name"].get<std::string>() + "'.";
                    state["samples"] = nlohmann::json::array();
                    state["switches"] = nlohmann::json::array();
                    return reply(std::move(state), summary);
                }

                const auto elapsedSeconds = [&start]()
                {
                    return std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
                };
                nlohmann::json samples = nlohmann::json::array();
                nlohmann::json first{{"t", 0.0f}, {"tick", state["tick"]}, {"status", state["status"]},
                                     {"leaf", state["activeLeaf"]}};
                {
                    std::string names;
                    for (const auto& node : state["activePath"])
                    {
                        names += (names.empty() ? "" : " > ") +
                                 (node["name"].is_string() ? node["name"].get<std::string>() : std::string("?"));
                    }
                    first["path"] = names;
                }
                samples.push_back(std::move(first));
                bool stopped = false;
                while (true)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
                    const float t = elapsedSeconds();
                    if (t > seconds || std::chrono::steady_clock::now() >= deadline)
                    {
                        break;
                    }
                    nlohmann::json sample = context.runOnMain([id]() { return debugStateOnMain(id, false); });
                    sample["t"] = env::floatJson(static_cast<float>(static_cast<int>(t * 1000.0f)) / 1000.0f);
                    const bool attached = sample.value("attached", false);
                    sample.erase("entity");
                    sample.erase("name");
                    sample.erase("mode");
                    sample.erase("attached");
                    samples.push_back(std::move(sample));
                    if (!attached)
                    {
                        stopped = true;  // Play stopped or the tree detached
                        break;
                    }
                }
                state = context.runOnMain([id]() { return debugStateOnMain(id, true); });

                nlohmann::json switches = nlohmann::json::array();
                std::vector<std::string> sequence;
                for (std::size_t i = 0; i < samples.size(); ++i)
                {
                    const nlohmann::json name = leafName(samples[i]);
                    if (i > 0 && leafId(samples[i]) != leafId(samples[i - 1]))
                    {
                        switches.push_back({{"t", samples[i]["t"]}, {"from", leafName(samples[i - 1])}, {"to", name}});
                    }
                    const std::string label = name.is_string() ? name.get<std::string>() : std::string("(none)");
                    if (sequence.empty() || sequence.back() != label)
                    {
                        sequence.push_back(label);
                    }
                }

                std::vector<std::string> warnings;
                if (stopped)
                {
                    warnings.emplace_back("the tree stopped running while it was sampled (Play stopped, or the tree "
                                          "detached)");
                }
                if (switches.size() >= 6 && switches.size() * 2 >= samples.size())
                {
                    warnings.emplace_back("the running leaf changes on most samples: a guarded branch that fails at once "
                                          "after its guard pre-empts thrashes every frame - its task must keep returning "
                                          "Running while the condition holds (vf://docs/bt section 5)");
                }
                const std::size_t sampleCount = samples.size();
                std::string summary;
                for (const std::string& label : sequence)
                {
                    summary += (summary.empty() ? "" : " -> ") + label;
                }
                state["samples"] = std::move(samples);
                state["switches"] = std::move(switches);
                state["sampling"] = {{"seconds", env::floatJson(static_cast<float>(seconds))},
                                     {"intervalMs", intervalMs},
                                     {"samples", sampleCount}};
                if (!warnings.empty())
                {
                    state["warnings"] = warnings;
                }
                return reply(std::move(state), countText(sampleCount, "sample") + ": " + summary + ".");
            };
            registry.add(std::move(tool));
        }
    }

    void registerBehaviorTreeTools(ToolRegistry& registry, const ToolContext& context)
    {
        registerListNodeTypes(registry, context);
        registerCreate(registry);
        registerGet(registry);
        registerSet(registry);
        registerEdit(registry);
        registerDebugState(registry, context);
        bttool::registerBehaviorTreeBlackboardTools(registry, context);
    }
}

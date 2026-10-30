#include "CoreTools.hpp"
#include "BehaviorTreeToolSupport.hpp"
#include "EnvironmentToolSupport.hpp"
#include "SettingsTable.hpp"
#include "ToolHelpers.hpp"
#include "../protocol/ArgReader.hpp"
#include "../undo/McpUndo.hpp"
#include "../undo/McpUndoCommands.hpp"

#include "events/EventDispatcher.hpp"
#include "events/ai/BehaviorTreeEvents.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// VK-1660 behavior-tree blackboard and assignment tools: bt_get_blackboard, bt_set_blackboard, bt_assign, bt_unassign.
// A tree's key definitions live in its .vfBehaviorTree (saved through the authoring command; allowed in Play mode, not
// undoable); an entity's start values live on its BehaviorTreeComponent and go through the "BehaviorTree" curated
// binding (BehaviorTreeBinding.cpp), whose generic Presence / Patch undo makes each call one undo step. Live values
// (bt_set_blackboard live:true) are written to the running tree with SetBlackboardValueCommand.
namespace mcp::tools::bttool
{
    namespace
    {
        constexpr const char* bindingName = "BehaviorTree";
        constexpr std::size_t maxValues = 64;

        std::string joinedNames(const std::vector<std::string>& names)
        {
            std::string out;
            for (const std::string& name : names)
            {
                out += (out.empty() ? "" : ", ") + name;
            }
            return out;
        }

        std::string describe(const services::EntityData& entity)
        {
            return std::to_string(toId(entity.handle)) + " ('" + entity.name + "')";
        }

        const nlohmann::json& requireValues(const ArgReader& reader)
        {
            const nlohmann::json& values = reader.raw("values");
            if (!values.is_object() || values.empty() || values.size() > maxValues)
            {
                throw ArgError("argument 'values' must be an object of 1-" + std::to_string(maxValues) +
                               " key -> value pairs");
            }
            return values;
        }

        std::vector<std::string> componentWarnings(const nlohmann::json& fields)
        {
            std::vector<std::string> out;
            if (const auto it = fields.find("warnings"); it != fields.end() && it->is_array())
            {
                for (const nlohmann::json& warning : *it)
                {
                    if (warning.is_string())
                    {
                        out.push_back(warning.get<std::string>());
                    }
                }
            }
            return out;
        }

        // ------------------------------------------------------------------
        // bt_get_blackboard
        // ------------------------------------------------------------------

        ToolResult treeBlackboard(const TreeFile& file)
        {
            const services::BehaviorTreeAssetLoadResult loaded = loadTree(file.enginePath);
            if (!loaded.success)
            {
                return ToolResult::error("Cannot read '" + file.relative + "': " + loaded.error);
            }
            const std::map<std::string, std::vector<KeyUse>> uses = keyUses(loaded.data.graph);
            nlohmann::json keys = nlohmann::json::array();
            for (const bt::BlackboardKeyDef& key : loaded.data.graph.blackboardKeys)
            {
                nlohmann::json entry = keySpecJson(key);
                const auto it = uses.find(key.name);
                entry["usedBy"] = it != uses.end() ? keyUsesJson(it->second) : nlohmann::json::array();
                keys.push_back(std::move(entry));
            }
            nlohmann::json inherited = nlohmann::json::array();
            const TreeFacts facts = treeFacts(file.enginePath);
            for (std::size_t i = 0; i < facts.keys.size(); ++i)
            {
                if (!facts.keySources[i].empty())
                {
                    nlohmann::json entry = keySpecJson(facts.keys[i]);
                    entry["from"] = facts.keySources[i];
                    inherited.push_back(std::move(entry));
                }
            }
            const std::size_t keyCount = keys.size();
            return reply({{"path", file.relative},
                          {"contentHash", hashHex(loaded.contentHash)},
                          {"keys", std::move(keys)},
                          {"inherited", std::move(inherited)},
                          {"note", "usedBy lists the nodes; scripts may also read and write keys by name "
                                   "(Blackboard::get* / set*)"}},
                         file.relative + ": " + std::to_string(keyCount) + " blackboard key(s).");
        }

        ToolResult entityBlackboard(const ArgReader& reader)
        {
            const services::EntityData entity = requireEntity(reader, "entity");
            const std::optional<services::BehaviorTreeComponentData> data = componentData(entity.handle);
            if (!data.has_value())
            {
                return ToolResult::error("Entity " + describe(entity) + " has no BehaviorTree component (bt_assign "
                                         "gives it one)");
            }
            const bool play = playMode();
            const TreeFacts facts = data->treePath.empty() ? TreeFacts{} : treeFacts(data->treePath);

            std::map<std::string, bt::BlackboardValue> live;
            bool attached = false;
            if (play)
            {
                try
                {
                    events::ai::GetBehaviorTreeRuntimeStateQuery query;
                    query.entity = entity.handle;
                    const bt::BTRuntimeState state = events::EventDispatcher::instance().query(query);
                    attached = state.attached;
                    for (const auto& [key, value] : state.snapshot.blackboard)
                    {
                        live[key] = value;
                    }
                }
                catch (const std::runtime_error&)
                {
                    // No behavior tree service: nothing runs.
                }
            }

            nlohmann::json keys = nlohmann::json::array();
            for (std::size_t i = 0; i < facts.keys.size(); ++i)
            {
                const bt::BlackboardKeyDef& key = facts.keys[i];
                nlohmann::json entry = keySpecJson(key);
                if (!facts.keySources[i].empty())
                {
                    entry["from"] = facts.keySources[i];
                }
                const auto stored = std::find_if(data->initialValues.begin(), data->initialValues.end(),
                                                 [&key](const services::BehaviorTreeInitialValueData& value)
                                                 {
                                                     return value.key == key.name;
                                                 });
                const bool fits = stored != data->initialValues.end() && valueType(stored->value) == key.type;
                if (stored != data->initialValues.end())
                {
                    entry["initial"] = startValueJson(*stored);
                }
                if (fits && stored->unresolved)
                {
                    entry["start"] = entityJson(services::EntityHandle::invalid());
                    entry["unresolved"] = true;
                }
                else
                {
                    entry["start"] = fits ? valueJson(stored->value) : entry["default"];
                }
                if (const auto it = live.find(key.name); it != live.end())
                {
                    entry["live"] = valueJson(it->second);
                    live.erase(it);
                }
                keys.push_back(std::move(entry));
            }

            nlohmann::json stale = nlohmann::json::array();
            for (const services::BehaviorTreeInitialValueData& value : data->initialValues)
            {
                const bt::BlackboardKeyDef* key = findKey(facts.keys, value.key);
                if (key != nullptr && key->type == valueType(value.value))
                {
                    continue;
                }
                stale.push_back({{"key", value.key},
                                 {"value", startValueJson(value)},
                                 {"reason", key == nullptr ? std::string("the tree has no such key")
                                                           : "the tree's key is " +
                                                             std::string(bt::blackboardValueTypeToString(key->type))}});
            }

            nlohmann::json out{{"entity", toId(entity.handle)},
                               {"name", entity.name},
                               {"tree", reportTreePath(data->treePath)},
                               {"enabled", data->enabled},
                               {"mode", play ? "play" : "edit"},
                               {"keys", std::move(keys)},
                               {"staleValues", std::move(stale)}};
            std::vector<std::string> notes;
            if (data->treePath.empty())
            {
                notes.emplace_back("no tree is assigned, so there are no keys (bt_assign sets one)");
            }
            else if (!facts.readable)
            {
                notes.push_back("the tree cannot be read: " + facts.error);
            }
            if (play)
            {
                out["attached"] = attached;
                if (!live.empty())
                {
                    nlohmann::json extra = nlohmann::json::object();
                    for (const auto& [key, value] : live)
                    {
                        extra[key] = valueJson(value);
                    }
                    out["liveOnly"] = std::move(extra);  // set at runtime (scripts), not declared by the tree
                }
            }
            else
            {
                notes.emplace_back("live values exist only in Play mode");
            }
            out["notes"] = notes;
            return reply(std::move(out), "Blackboard of " + describe(entity) + ".");
        }

        void registerGetBlackboard(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "bt_get_blackboard";
            tool.title = "Get behavior tree blackboard";
            tool.description =
                "Read blackboard keys. {path}: the tree's keys {name, type, default, usedBy [{id, name, type, param}]} "
                "and 'inherited' (keys its SubTrees add, with 'from'). {entity}: the keys of the tree on the entity's "
                "BehaviorTree component with this entity's values - initial (its stored start value; Entity values as "
                "{entity, name}, unresolved:true when that entity is gone), start (what the blackboard starts with: "
                "initial, else default) and, in Play mode, live (the running tree's value); staleValues lists stored "
                "values that no longer fit the tree. Works in any mode.";
            tool.inputSchema = closedObject({
                {"path", schema::string(".vfBehaviorTree path relative to the project's asset folder")},
                {"entity", schema::entity("Entity whose BehaviorTree component to read instead")}
            });
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"path", "entity"}, "bt_get_blackboard");
                const ArgReader reader(args);
                if (reader.has("entity") && !reader.has("path"))
                {
                    return entityBlackboard(reader);
                }
                return treeBlackboard(treeFromArguments(reader, "bt_get_blackboard"));
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // bt_set_blackboard
        // ------------------------------------------------------------------

        ToolResult setTreeKeys(const ArgReader& reader)
        {
            const TreeFile file = resolveTreeArgument(reader.requireString("path"), "argument 'path'", true);
            const nlohmann::json* keys = reader.has("keys") ? &reader.raw("keys") : nullptr;
            const nlohmann::json* removeKeys = reader.has("removeKeys") ? &reader.raw("removeKeys") : nullptr;
            if (keys == nullptr && removeKeys == nullptr)
            {
                throw ArgError("bt_set_blackboard {path} needs 'keys' (key definitions to add or replace) and/or "
                               "'removeKeys' (names)");
            }
            const services::BehaviorTreeAssetLoadResult loaded = loadTree(file.enginePath);
            if (!loaded.success)
            {
                return ToolResult::error("Cannot read '" + file.relative + "': " + loaded.error);
            }
            const std::optional<uint64_t> expected = optHash(reader, "expectedContentHash");
            if (expected.has_value() && *expected != loaded.contentHash)
            {
                return ToolResult::error("'" + file.relative + "' changed on disk since it was read (its contentHash is "
                                         "now " + hashHex(loaded.contentHash) + "); bt_get it again");
            }

            KeyEditOutcome edit = applyKeyEdits(loaded.data, keys, removeKeys);
            if (sameTreeContent(edit.data, loaded.data))
            {
                return reply({{"path", file.relative}, {"changed", false},
                              {"contentHash", hashHex(loaded.contentHash)},
                              {"keys", keySpecsJson(loaded.data.graph.blackboardKeys)}, {"reloadedRuntimes", 0},
                              {"warnings", edit.warnings}},
                             file.relative + ": the keys are unchanged.");
            }
            const services::BehaviorTreeAssetSaveResult result =
                saveTree(file, edit.data, loaded.contentHash, false, edit.strictNodes);
            if (!result.success)
            {
                return saveFailure(file, result);
            }
            std::vector<std::string> warnings = std::move(edit.warnings);
            for (std::string& text : diagnosticTexts(result.diagnostics, false))
            {
                warnings.push_back(std::move(text));
            }
            const int reloaded = liveRuntimesUsing(file.enginePath);
            return reply({{"path", file.relative}, {"changed", true}, {"contentHash", hashHex(result.contentHash)},
                          {"keys", keySpecsJson(edit.data.graph.blackboardKeys)}, {"reloadedRuntimes", reloaded},
                          {"warnings", warnings}},
                         "Saved the blackboard keys of " + file.relative + ".");
        }

        ToolResult setLiveValues(const ArgReader& reader)
        {
            if (!playMode())
            {
                return ToolResult::error("live:true writes the running tree, so it needs Play mode (play_start); "
                                         "without it the values are this entity's start values");
            }
            const services::EntityData entity = requireEntity(reader, "entity");
            const nlohmann::json& values = requireValues(reader);

            events::ai::GetBehaviorTreeRuntimeStateQuery query;
            query.entity = entity.handle;
            const bt::BTRuntimeState state = events::EventDispatcher::instance().query(query);
            if (!state.attached || !state.tree)
            {
                return ToolResult::error("Entity " + describe(entity) + " has no running behavior tree "
                                         "(bt_debug_state says why)");
            }
            const std::vector<bt::BlackboardKeyDef>& keys = state.tree->graph.blackboardKeys;

            SpecErrors errors;
            std::vector<std::pair<std::string, bt::BlackboardValue>> writes;
            for (auto it = values.begin(); it != values.end(); ++it)
            {
                const std::string path = "values." + it.key();
                if (it->is_null())
                {
                    errors.add(path, "a live value cannot be removed; give a value");
                    continue;
                }
                const bt::BlackboardKeyDef* key = findKey(keys, it.key());
                if (key == nullptr)
                {
                    std::vector<std::string> names;
                    for (const bt::BlackboardKeyDef& candidate : keys)
                    {
                        names.push_back(candidate.name);
                    }
                    errors.add(path, "the running tree has no key '" + it.key() + "' (keys: " +
                                     (names.empty() ? std::string("none") : joinedNames(names)) + ")");
                    continue;
                }
                if (std::optional<bt::BlackboardValue> value = parseValue(*it, key->type, path, errors))
                {
                    writes.emplace_back(it.key(), std::move(*value));
                }
            }
            errors.throwIfAny("argument 'values'");

            nlohmann::json written = nlohmann::json::object();
            for (const auto& [key, value] : writes)
            {
                events::ai::SetBlackboardValueCommand command;
                command.entity = entity.handle;
                command.key = key;
                command.value = value;
                events::EventDispatcher::instance().execute(command);
                written[key] = valueJson(value);
            }
            return reply({{"entity", toId(entity.handle)}, {"name", entity.name}, {"live", true},
                          {"set", std::move(written)},
                          {"note", "not undoable; the running tree's values are gone when Play stops"}},
                         "Set " + std::to_string(writes.size()) + " live value(s) on " + describe(entity) + ".");
        }

        ToolResult setStartValues(const ArgReader& reader)
        {
            if (const std::optional<std::string> blocked = env::editBlocker("set an entity's blackboard start values"))
            {
                return ToolResult::error(*blocked + " (live:true writes the running tree instead)");
            }
            const services::EntityData entity = requireEntity(reader, "entity");
            const nlohmann::json& values = requireValues(reader);
            const std::optional<nlohmann::json> before = getBuiltinComponent(bindingName, entity.handle);
            if (!before.has_value())
            {
                return ToolResult::error("Entity " + describe(entity) + " has no BehaviorTree component (bt_assign "
                                         "gives it one, with its tree)");
            }
            const std::optional<nlohmann::json> after =
                patchBuiltinComponent(bindingName, entity.handle, nlohmann::json{{"blackboard", values}});
            if (!after.has_value())
            {
                return ToolResult::error("Entity " + describe(entity) + " lost its BehaviorTree component");
            }
            const bool changed = *before != *after;
            if (changed)
            {
                undo::UndoRecorder recorder("MCP: Set blackboard start values (" + entity.name + ")");
                recorder.add(std::make_unique<undo::BuiltinComponentPatchUndo>(toId(entity.handle), bindingName,
                                                                               *before, *after));
                recorder.push();
            }
            return reply({{"entity", toId(entity.handle)}, {"name", entity.name}, {"changed", changed},
                          {"blackboard", (*after)["blackboard"]}, {"warnings", componentWarnings(*after)}},
                         changed ? "Set the start values of " + describe(entity) + "."
                                 : "The start values of " + describe(entity) + " are unchanged.");
        }

        void registerSetBlackboard(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "bt_set_blackboard";
            tool.title = "Set behavior tree blackboard";
            tool.description =
                "Change blackboard keys or values. {path, keys?: [{name, type: Float|Int|Bool|String|Vec3|Entity, "
                "default?}], removeKeys?: [name], expectedContentHash?} adds or replaces the tree's key definitions "
                "(a type change re-checks the nodes that use the key) and removes keys; removing a key a node still "
                "uses is refused and names those nodes. It saves the tree: allowed in Play mode (running trees reload), "
                "not undoable. {entity, values: {key: value | null}} sets this entity's start values (merged; null "
                "removes one; each must fit the type of the tree's key; Entity values as an entity id or {entity}): one "
                "undo step, refused in Play mode and while a scene load is pending. {entity, values, live:true} (Play "
                "mode only) writes the running tree's blackboard instead: type-checked, not undoable, gone when Play "
                "stops. Returns the keys, or the values with warnings.";
            tool.inputSchema = closedObject({
                {"path", schema::string(".vfBehaviorTree path relative to the project's asset folder (key definitions)")},
                {"keys", schema::array(schema::anyObject("{name, type, default?}"), "Key definitions to add or replace")},
                {"removeKeys", schema::array(schema::string("Key name"), "Keys to remove")},
                {"expectedContentHash", schema::string("contentHash from bt_get: refuse when the file changed since")},
                {"entity", schema::entity("Entity whose values to set (per-entity start values, or live values)")},
                {"values", schema::anyObject("key -> value (null removes a start value)")},
                {"live", schema::boolean("Write the running tree's values (Play mode). Default false.")}
            });
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"path", "keys", "removeKeys", "expectedContentHash", "entity", "values",
                                              "live"}, "bt_set_blackboard");
                const ArgReader reader(args);
                const bool treeMode = reader.has("path");
                if (treeMode == reader.has("entity"))
                {
                    throw ArgError("bt_set_blackboard takes either 'path' (the tree's key definitions) or 'entity' "
                                   "(an entity's values)");
                }
                if (treeMode)
                {
                    for (const char* name : {"values", "live"})
                    {
                        if (reader.has(name))
                        {
                            throw ArgError(std::string("argument '") + name + "' belongs to {entity, values}, not to "
                                           "{path, keys, removeKeys}");
                        }
                    }
                    return setTreeKeys(reader);
                }
                for (const char* name : {"keys", "removeKeys", "expectedContentHash"})
                {
                    if (reader.has(name))
                    {
                        throw ArgError(std::string("argument '") + name + "' belongs to {path, keys, removeKeys}, not "
                                       "to {entity, values}");
                    }
                }
                return reader.optBool("live", false) ? setLiveValues(reader) : setStartValues(reader);
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // bt_assign / bt_unassign
        // ------------------------------------------------------------------

        void registerAssign(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "bt_assign";
            tool.title = "Assign behavior tree";
            tool.description =
                "Give an entity a behavior tree: adds its BehaviorTree component (or updates the existing one) with "
                "'tree' (a .vfBehaviorTree path relative to the asset folder), enabled (default true) and per-entity "
                "start values 'blackboard' {key: value} (each must fit the type of the tree's key; Entity values as an "
                "entity id or {entity}; merged into the stored values, and stored values that do not fit the new tree "
                "are dropped). The tree attaches when Play starts. Warns when the tree moves the entity (MoveTo, "
                "FollowRoute) but it has no NavmeshAgent or VolumetricAgent, when a NavmeshAgent sits below a parent, "
                "about Entity keys nothing sets, and about dropped values. One undo step. Refused in Play mode and "
                "while a scene load is pending. Returns {entity, name, tree, enabled, added, changed, blackboard, "
                "warnings}.";
            tool.inputSchema = closedObject({
                {"entity", schema::entity("Entity that runs the tree")},
                {"tree", schema::string(".vfBehaviorTree path relative to the project's asset folder")},
                {"enabled", schema::boolean("Run the tree when Play starts. Default true.")},
                {"blackboard", schema::anyObject("Start values: key -> value (Entity values as an entity id)")}
            }, {"entity", "tree"});
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"entity", "tree", "enabled", "blackboard"}, "bt_assign");
                if (const std::optional<std::string> blocked = env::editBlocker("assign a behavior tree"))
                {
                    return ToolResult::error(*blocked);
                }
                const ArgReader reader(args);
                const services::EntityData entity = requireEntity(reader, "entity");
                rejectRoot(entity, "assign a behavior tree to");
                rejectTerrain(entity, "assign a behavior tree to", "Give the tree to an ordinary entity.");
                const TreeFile file = resolveTreeArgument(reader.requireString("tree"), "argument 'tree'", true);
                const bool enabled = reader.optBool("enabled", true);
                nlohmann::json values = nlohmann::json::object();
                if (reader.has("blackboard"))
                {
                    values = reader.raw("blackboard");
                    if (!values.is_object() || values.size() > maxValues)
                    {
                        throw ArgError("argument 'blackboard' must be an object of up to " + std::to_string(maxValues) +
                                       " key -> value pairs");
                    }
                }
                const TreeFacts facts = treeFacts(file.enginePath);
                if (!facts.readable)
                {
                    return ToolResult::error("Cannot read '" + file.relative + "': " + facts.error);
                }

                // Stored values the new tree has no matching key for are dropped (unless given again).
                const std::optional<services::BehaviorTreeComponentData> current = componentData(entity.handle);
                std::vector<std::string> dropped;
                if (current.has_value())
                {
                    for (const services::BehaviorTreeInitialValueData& value : current->initialValues)
                    {
                        if (values.contains(value.key))
                        {
                            continue;
                        }
                        const bt::BlackboardKeyDef* key = findKey(facts.keys, value.key);
                        if (key == nullptr || key->type != valueType(value.value))
                        {
                            dropped.push_back(value.key);
                            values[value.key] = nullptr;
                        }
                    }
                }
                const nlohmann::json patch{{"tree", file.relative}, {"enabled", enabled}, {"blackboard", values}};

                undo::UndoRecorder recorder("MCP: Assign behavior tree (" + entity.name + ")");
                std::optional<nlohmann::json> fields;
                bool added = false;
                bool changed = true;
                if (!current.has_value())
                {
                    if (!addBuiltinComponent(bindingName, entity.handle))
                    {
                        return ToolResult::error("AddBehaviorTreeComponent failed for entity " + describe(entity));
                    }
                    try
                    {
                        fields = patchBuiltinComponent(bindingName, entity.handle, patch);
                        if (!fields.has_value())
                        {
                            throw std::runtime_error("Entity " + describe(entity) + " lost its new BehaviorTree component");
                        }
                    }
                    catch (...)
                    {
                        // Never leave a default-valued component behind.
                        removeBuiltinComponent(bindingName, entity.handle);
                        throw;
                    }
                    recorder.add(std::make_unique<undo::BuiltinComponentPresenceUndo>(toId(entity.handle), bindingName,
                                                                                      *fields, true));
                    added = true;
                }
                else
                {
                    const std::optional<nlohmann::json> before = getBuiltinComponent(bindingName, entity.handle);
                    fields = patchBuiltinComponent(bindingName, entity.handle, patch);
                    if (!before.has_value() || !fields.has_value())
                    {
                        return ToolResult::error("Entity " + describe(entity) + " lost its BehaviorTree component");
                    }
                    changed = *before != *fields;
                    if (changed)
                    {
                        recorder.add(std::make_unique<undo::BuiltinComponentPatchUndo>(toId(entity.handle), bindingName,
                                                                                       *before, *fields));
                    }
                }
                recorder.push();

                std::vector<std::string> warnings;
                const bool navmeshAgent = entity.hasComponent(services::ComponentTypeId::NavmeshAgent);
                const bool volumetricAgent = entity.hasComponent(services::ComponentTypeId::VolumetricAgent);
                if (facts.moves && !navmeshAgent && !volumetricAgent)
                {
                    warnings.emplace_back("the tree moves the entity (MoveTo / FollowRoute) but it has neither a "
                                          "NavmeshAgent nor a VolumetricAgent: those nodes fail until component_add "
                                          "{type:\"NavmeshAgent\"} gives it one (vf://docs/navigation)");
                }
                if (navmeshAgent && entity.parent.has_value() && entity.parent->isValid() &&
                    *entity.parent != sceneRoot())
                {
                    const std::optional<services::EntityData> parent = findEntity(*entity.parent);
                    warnings.push_back("the NavmeshAgent sits below '" + (parent.has_value() ? parent->name : std::string("?")) +
                                       "': the crowd writes the agent's LOCAL position, so keep agents at the scene root "
                                       "(entity_set_parent)");
                }
                if (!dropped.empty())
                {
                    warnings.push_back("dropped the start value(s) " + joinedNames(dropped) + ": they do not fit the keys "
                                       "of " + file.relative);
                }
                for (std::string& warning : componentWarnings(*fields))
                {
                    warnings.push_back(std::move(warning));
                }

                const std::string summary = (added ? "Assigned " : changed ? "Updated " : "Unchanged: ") + file.relative +
                                            " on " + describe(entity) + ".";
                return reply({{"entity", toId(entity.handle)},
                              {"name", entity.name},
                              {"tree", file.relative},
                              {"enabled", enabled},
                              {"added", added},
                              {"changed", changed},
                              {"blackboard", (*fields)["blackboard"]},
                              {"warnings", warnings}},
                             summary);
            };
            registry.add(std::move(tool));
        }

        void registerUnassign(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "bt_unassign";
            tool.title = "Unassign behavior tree";
            tool.description =
                "Remove an entity's BehaviorTree component (the tree file is kept). One undo step: undo restores the "
                "component with its tree and start values. Refused in Play mode and while a scene load is pending. "
                "Returns {entity, name, removed, tree}.";
            tool.inputSchema = closedObject({
                {"entity", schema::entity("Entity whose BehaviorTree component to remove")}
            }, {"entity"});
            tool.destructive = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"entity"}, "bt_unassign");
                if (const std::optional<std::string> blocked = env::editBlocker("unassign a behavior tree"))
                {
                    return ToolResult::error(*blocked);
                }
                const ArgReader reader(args);
                const services::EntityData entity = requireEntity(reader, "entity");
                std::optional<nlohmann::json> fields = getBuiltinComponent(bindingName, entity.handle);
                if (!fields.has_value())
                {
                    return ToolResult::error("Entity " + describe(entity) + " has no BehaviorTree component");
                }
                if (!removeBuiltinComponent(bindingName, entity.handle))
                {
                    return ToolResult::error("RemoveBehaviorTreeComponent failed for entity " + describe(entity));
                }
                const nlohmann::json tree = (*fields)["tree"];
                undo::UndoRecorder recorder("MCP: Unassign behavior tree (" + entity.name + ")");
                recorder.add(std::make_unique<undo::BuiltinComponentPresenceUndo>(toId(entity.handle), bindingName,
                                                                                  std::move(*fields), false));
                recorder.push();
                return reply({{"entity", toId(entity.handle)}, {"name", entity.name}, {"removed", true},
                              {"tree", tree}},
                             "Removed the BehaviorTree component of " + describe(entity) + ".");
            };
            registry.add(std::move(tool));
        }
    }

    void registerBehaviorTreeBlackboardTools(ToolRegistry& registry, const ToolContext& context)
    {
        (void)context;
        registerGetBlackboard(registry);
        registerSetBlackboard(registry);
        registerAssign(registry);
        registerUnassign(registry);
    }
}

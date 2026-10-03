#include "CoreTools.hpp"
#include "ContentHelpers.hpp"
#include "ToolHelpers.hpp"
#include "../protocol/ArgReader.hpp"
#include "../undo/McpUndo.hpp"
#include "../undo/McpUndoCommands.hpp"

#include "events/EventDispatcher.hpp"
#include "events/scene/PluginComponentEvents.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace mcp::tools
{
    namespace
    {
        constexpr const char* fieldFormat =
            " Field values use the scene-file JSON format: numbers, booleans, strings, vectors as [x,y,(z,w)], "
            "nested structs as objects (merged), containers as arrays/objects; an asset field takes the "
            "asset GUID hex under its own name or a path under '<name>Path'. List the types and their fields "
            "with component_list_types.";

        std::string requireType(const ArgReader& reader)
        {
            std::string type = reader.requireString("type");
            if (type.empty())
            {
                throw ArgError("argument 'type' must not be empty");
            }
            return type;
        }

        const nlohmann::json* optFieldsObject(const ArgReader& reader)
        {
            if (!reader.has("fields"))
            {
                return nullptr;
            }
            const nlohmann::json& fields = reader.raw("fields");
            if (!fields.is_object())
            {
                throw ArgError("argument 'fields' must be an object of field -> value");
            }
            return &fields;
        }

        // `source` restricted to the keys of `keys`.
        nlohmann::json pick(const nlohmann::json& source, const nlohmann::json& keys)
        {
            nlohmann::json out = nlohmann::json::object();
            for (auto it = keys.begin(); it != keys.end(); ++it)
            {
                auto found = source.find(it.key());
                if (found != source.end())
                {
                    out[it.key()] = *found;
                }
            }
            return out;
        }

        // The keys an undo entry must carry for the agent's `fields`. An asset written
        // only as "<name>Path" also needs "<name>" (GUID hex): Get emits the Path
        // sibling only when the old ref resolved to a path, so an unset or unresolved
        // old ref would otherwise restore nothing. "<name>" wins over the path on Set.
        nlohmann::json undoKeys(const nlohmann::json& fields, const nlohmann::json& before)
        {
            static constexpr std::string_view pathSuffix = "Path";
            nlohmann::json keys = fields;
            for (auto it = fields.begin(); it != fields.end(); ++it)
            {
                const std::string& key = it.key();
                if (key.size() > pathSuffix.size() && key.ends_with(pathSuffix))
                {
                    const std::string base = key.substr(0, key.size() - pathSuffix.size());
                    if (before.contains(base) && !keys.contains(base))
                    {
                        keys[base] = nullptr;
                    }
                }
            }
            return keys;
        }

        nlohmann::json componentResult(const services::EntityHandle& entity, const std::string& type,
                                       nlohmann::json fields)
        {
            return {{"entity", entityId(entity)}, {"type", type}, {"fields", std::move(fields)}};
        }

        void registerComponentListTypes(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "component_list_types";
            tool.title = "List component types";
            tool.description =
                "List every component type the agent can edit. 'builtin' = the curated engine components "
                "(component_add / _get / _set / _remove) with their field help; 'plugin' = reflected plugin "
                "components (component_*_generic) as [{name, plugin, fields:[{name, type, label?, readOnly?, "
                "hidden?, min?, max?}]}].";
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json&) -> ToolResult
            {
                nlohmann::json builtin = nlohmann::json::array();
                for (const BuiltinComponentInfo& info : builtinComponentTypes())
                {
                    builtin.push_back({{"name", info.name}, {"fieldHelp", info.fieldHelp}});
                }
                return ToolResult::ok({{"builtin", std::move(builtin)}, {"plugin", pluginComponentTypes()}});
            };
            registry.add(std::move(tool));
        }

        void registerComponentGetGeneric(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "component_get_generic";
            tool.title = "Get plugin component";
            tool.description =
                "Read all fields of a plugin component on an entity. Returns {entity, type, fields}." +
                std::string(fieldFormat);
            tool.inputSchema = schema::object({
                {"entity", schema::entity()},
                {"type", schema::string("Plugin component type name (from component_list_types)")}
            }, {"entity", "type"});
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                const services::EntityData entity = requireEntity(reader, "entity");
                const std::string type = requireType(reader);

                events::scene::GetPluginComponentQuery query;
                query.entity = entity.handle;
                query.type = type;
                std::optional<nlohmann::json> fields = events::EventDispatcher::instance().query(query);
                if (!fields.has_value())
                {
                    return ToolResult::error("Entity has no '" + type +
                                             "' component, or no plugin registers that type (see component_list_types)");
                }
                return ToolResult::ok(componentResult(entity.handle, type, std::move(*fields)));
            };
            registry.add(std::move(tool));
        }

        void registerComponentSetGeneric(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "component_set_generic";
            tool.title = "Set plugin component fields";
            tool.description =
                "Partially update a plugin component: only the keys in 'fields' change. Strict: unknown, "
                "read-only or mistyped fields fail the whole call and nothing is written. Undoable. Returns "
                "{entity, type, fields} with every field after the update." + std::string(fieldFormat);
            tool.inputSchema = schema::object({
                {"entity", schema::entity()},
                {"type", schema::string("Plugin component type name (from component_list_types)")},
                {"fields", schema::anyObject("Field values to change (partial)")}
            }, {"entity", "type", "fields"});
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                const services::EntityData entity = requireEntity(reader, "entity");
                const std::string type = requireType(reader);
                const nlohmann::json* fields = optFieldsObject(reader);
                if (fields == nullptr || fields->empty())
                {
                    throw ArgError("argument 'fields' must be a non-empty object of field -> value");
                }

                auto& dispatcher = events::EventDispatcher::instance();
                events::scene::GetPluginComponentQuery query;
                query.entity = entity.handle;
                query.type = type;
                std::optional<nlohmann::json> before = dispatcher.query(query);
                if (!before.has_value())
                {
                    return ToolResult::error("Entity has no '" + type + "' component; use component_add_generic");
                }

                events::scene::SetPluginComponentFieldsCommand command;
                command.entity = entity.handle;
                command.type = type;
                command.fields = *fields;
                events::scene::PluginComponentResult result = dispatcher.execute(command);
                if (!result.ok)
                {
                    return ToolResult::error(result.error);
                }

                // Only the keys the agent wrote (plus asset GUID keys, see undoKeys):
                // replaying the full object would hit read-only fields, which the strict
                // setter rejects.
                const nlohmann::json keys = undoKeys(*fields, *before);
                nlohmann::json after = pick(result.value, keys);
                for (auto it = fields->begin(); it != fields->end(); ++it)
                {
                    if (!after.contains(it.key()))
                    {
                        after[it.key()] = it.value();
                    }
                }
                undo::UndoRecorder recorder("MCP: Set " + type + " fields");
                recorder.add(std::make_unique<undo::PluginComponentPatchUndo>(
                    toId(entity.handle), type, pick(*before, keys), std::move(after)));
                recorder.push();
                return ToolResult::ok(componentResult(entity.handle, type, std::move(result.value)));
            };
            registry.add(std::move(tool));
        }

        void registerComponentAddGeneric(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "component_add_generic";
            tool.title = "Add plugin component";
            tool.description =
                "Add a plugin component to an entity, optionally initialising fields (same strict rules as "
                "component_set_generic; on a field error nothing is added). Fails if the entity already has it. "
                "Undoable. Returns {entity, type, fields}." + std::string(fieldFormat);
            tool.inputSchema = schema::object({
                {"entity", schema::entity()},
                {"type", schema::string("Plugin component type name (from component_list_types)")},
                {"fields", schema::anyObject("Optional initial field values (partial)")}
            }, {"entity", "type"});
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                const services::EntityData entity = requireEntity(reader, "entity");
                const std::string type = requireType(reader);
                const nlohmann::json* fields = optFieldsObject(reader);

                events::scene::AddPluginComponentCommand command;
                command.entity = entity.handle;
                command.type = type;
                if (fields != nullptr)
                {
                    command.fields = *fields;
                }
                events::scene::PluginComponentResult result = events::EventDispatcher::instance().execute(command);
                if (!result.ok)
                {
                    return ToolResult::error(result.error);
                }

                // Redo replays the agent's own (already validated) arguments.
                undo::UndoRecorder recorder("MCP: Add " + type + " component");
                recorder.add(std::make_unique<undo::PluginComponentPresenceUndo>(
                    toId(entity.handle), type, command.fields, true));
                recorder.push();
                return ToolResult::ok(componentResult(entity.handle, type, std::move(result.value)));
            };
            registry.add(std::move(tool));
        }

        void registerComponentRemoveGeneric(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "component_remove_generic";
            tool.title = "Remove plugin component";
            tool.description =
                "Remove a plugin component from an entity. Undoable: undo re-adds it with its writable field "
                "values. Returns {entity, type, fields} with the values it had.";
            tool.inputSchema = schema::object({
                {"entity", schema::entity()},
                {"type", schema::string("Plugin component type name (from component_list_types)")}
            }, {"entity", "type"});
            tool.destructive = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                const services::EntityData entity = requireEntity(reader, "entity");
                const std::string type = requireType(reader);

                events::scene::RemovePluginComponentCommand command;
                command.entity = entity.handle;
                command.type = type;
                events::scene::PluginComponentResult result = events::EventDispatcher::instance().execute(command);
                if (!result.ok)
                {
                    return ToolResult::error(result.error);
                }

                undo::UndoRecorder recorder("MCP: Remove " + type + " component");
                recorder.add(std::make_unique<undo::PluginComponentPresenceUndo>(
                    toId(entity.handle), type, undo::writablePluginFields(type, result.value), false));
                recorder.push();
                return ToolResult::ok(componentResult(entity.handle, type, std::move(result.value)));
            };
            registry.add(std::move(tool));
        }
    }

    nlohmann::json pluginComponentTypes()
    {
        nlohmann::json plugin =
            events::EventDispatcher::instance().query(events::scene::GetPluginComponentTypesQuery{});
        if (!plugin.is_array())
        {
            plugin = nlohmann::json::array();
        }
        return plugin;
    }

    void registerPluginComponentTools(ToolRegistry& registry, const ToolContext&)
    {
        registerComponentListTypes(registry);
        registerComponentGetGeneric(registry);
        registerComponentSetGeneric(registry);
        registerComponentAddGeneric(registry);
        registerComponentRemoveGeneric(registry);
    }
}

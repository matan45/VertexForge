#include "CoreTools.hpp"
#include "ToolHelpers.hpp"
#include "../protocol/ArgReader.hpp"
#include "../undo/McpUndo.hpp"
#include "../undo/McpUndoCommands.hpp"

#include "events/EventDispatcher.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace mcp::tools
{
    namespace
    {
        // VK-1653 rejectTerrain hints.
        constexpr const char* deleteTerrainHint =
            "Remove a terrain with terrain_delete (a tile cannot be removed on its own).";
        constexpr const char* duplicateTerrainHint =
            "Create another terrain with terrain_create instead (a duplicate would reload the same .vfTerrain).";
        constexpr const char* moveTerrainHint =
            "Terrains and their tiles keep their place in the hierarchy; the tile grid is centred on the world origin.";
        constexpr const char* transformTerrainHint =
            "The tile grid is placed by the terrain itself; reshape it with terrain_sculpt / terrain_generate_heightmap.";
        constexpr const char* parentTerrainHint =
            "Create the entity elsewhere (e.g. at the scene root) and stand it on the ground with terrain_height_at.";

        std::string lowercase(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        void registerEntityFind(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "entity_find";
            tool.title = "Find entities";
            tool.description =
                "Find entities by name. Default: case-insensitive substring match over the scene hierarchy. "
                "exact=true: exact, case-sensitive name match. Returns [{id, name, parent}] (at most 'limit').";
            tool.inputSchema = schema::object({
                {"name", schema::string("Name or name fragment to search for")},
                {"exact", schema::boolean("Exact case-sensitive match. Default false.")},
                {"limit", schema::integer("Maximum results. Default 100.")}
            }, {"name"});
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                std::string name = reader.requireString("name");
                bool exact = reader.optBool("exact", false);
                int64_t limit = reader.optInt("limit", 100);
                if (limit < 1)
                {
                    throw ArgError("argument 'limit' must be >= 1");
                }

                auto& dispatcher = events::EventDispatcher::instance();
                nlohmann::json matches = nlohmann::json::array();
                std::size_t total = 0;

                auto addMatch = [&](const services::EntityData& entity)
                {
                    ++total;
                    if (static_cast<int64_t>(matches.size()) < limit)
                    {
                        matches.push_back({
                            {"id", entityId(entity.handle)},
                            {"name", entity.name},
                            {"parent", entityId(entity.parent)}
                        });
                    }
                };

                if (exact)
                {
                    events::scene::FindEntitiesByNameQuery query;
                    query.name = name;
                    const services::EntityHandle root = sceneRoot();
                    for (const services::EntityHandle& handle : dispatcher.query(query))
                    {
                        if (handle == root)
                        {
                            continue;  // same as the substring branch: never report the scene root
                        }
                        events::scene::GetEntityQuery entityQuery;
                        entityQuery.entity = handle;
                        if (auto entity = dispatcher.query(entityQuery))
                        {
                            addMatch(*entity);
                        }
                    }
                }
                else
                {
                    const std::string needle = lowercase(name);
                    services::SceneHierarchyData hierarchy = dispatcher.query(events::scene::GetSceneHierarchyQuery{});
                    for (const services::EntityData& entity : hierarchy.entities)
                    {
                        if (entity.handle == hierarchy.root)
                        {
                            continue;
                        }
                        if (lowercase(entity.name).find(needle) != std::string::npos)
                        {
                            addMatch(entity);
                        }
                    }
                }

                return ToolResult::ok({{"matches", std::move(matches)}, {"total", total}});
            };
            registry.add(std::move(tool));
        }

        void registerEntityCreate(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "entity_create";
            tool.title = "Create entity";
            tool.description =
                "Create an empty entity (transform only) under 'parent' (default: scene root) with an optional "
                "local transform. Add components with component_add / material_assign / script_attach. "
                "A terrain or terrain tile cannot be the parent. "
                "Undoable (one undo step). Returns {id, name, parent, transform}.";
            nlohmann::json properties = transformProperties();
            properties["name"] = schema::string("Entity name. Default 'Entity'.");
            properties["parent"] = schema::entity("Parent entity id. Omit for the scene root.");
            tool.inputSchema = schema::object(std::move(properties));
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                auto& dispatcher = events::EventDispatcher::instance();

                events::scene::CreateEntityCommand command;
                command.name = reader.optString("name", "Entity");
                if (command.name.empty())
                {
                    throw ArgError("argument 'name' must not be empty");
                }
                if (reader.has("parent"))
                {
                    const services::EntityData parent = requireEntity(reader, "parent");
                    rejectTerrain(parent, "create a child under", parentTerrainHint);
                    command.parent = parent.handle;
                }
                // Validate vectors before creating anything so a bad argument leaves no orphan.
                reader.optVec3("position");
                reader.optVec3("rotationEuler");
                reader.optVec3("scale");

                services::EntityHandle created = dispatcher.execute(command);
                if (!created.isValid())
                {
                    return ToolResult::error("CreateEntity failed");
                }

                nlohmann::json out{
                    {"id", entityId(created)},
                    {"name", command.name},
                    {"parent", entityId(command.parent)}
                };
                if (hasTransformArgs(reader))
                {
                    out["transform"] = transformToJson(applyTransform(reader, created).after);
                }

                // Snapshot after the transform so one entry covers the whole creation.
                undo::UndoRecorder recorder("MCP: Create entity (" + command.name + ")");
                recorder.add(undo::EntityLifetimeUndo::capture(undo::EntityLifetimeUndo::Kind::Created, created));
                recorder.push();
                return ToolResult::ok(std::move(out));
            };
            registry.add(std::move(tool));
        }

        void registerEntityDelete(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "entity_delete";
            tool.title = "Delete entity";
            tool.description =
                "Delete an entity together with all of its children. Their ids become invalid. Undoable: undo "
                "re-creates the subtree with NEW ids (undo/redo report them in remappedEntities) and fresh UUIDs. "
                "Refused for terrains and their tiles (or a subtree containing one): use terrain_delete.";
            tool.inputSchema = schema::object({{"entity", schema::entity()}}, {"entity"});
            tool.destructive = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                services::EntityData entity = requireEntity(reader, "entity");
                rejectRoot(entity, "delete");
                rejectTerrainSubtree(entity, "delete", deleteTerrainHint);

                // Snapshot before the delete; it is only pushed if the delete succeeds.
                undo::UndoRecorder recorder("MCP: Delete entity (" + entity.name + ")");
                recorder.add(undo::EntityLifetimeUndo::capture(undo::EntityLifetimeUndo::Kind::Deleted, entity.handle));

                events::scene::DeleteEntityCommand command;
                command.entity = entity.handle;
                if (!events::EventDispatcher::instance().execute(command))
                {
                    return ToolResult::error("DeleteEntity failed");
                }
                recorder.push();
                return ToolResult::ok({{"deleted", entityId(entity.handle)}, {"name", entity.name}});
            };
            registry.add(std::move(tool));
        }

        void registerEntityDuplicate(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "entity_duplicate";
            tool.title = "Duplicate entity";
            tool.description = "Duplicate an entity and its whole subtree (components included) under the same parent. Undoable. "
                                "Refused for terrains and their tiles (or a subtree containing one). "
                                "Returns the new root id.";
            tool.inputSchema = schema::object({{"entity", schema::entity()}}, {"entity"});
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                services::EntityData entity = requireEntity(reader, "entity");
                rejectRoot(entity, "duplicate");
                rejectTerrainSubtree(entity, "duplicate", duplicateTerrainHint);

                events::scene::DuplicateEntityCommand command;
                command.entity = entity.handle;
                services::EntityHandle duplicate = events::EventDispatcher::instance().execute(command);
                if (!duplicate.isValid())
                {
                    return ToolResult::error("DuplicateEntity failed");
                }

                undo::UndoRecorder recorder("MCP: Duplicate entity (" + entity.name + ")");
                recorder.add(undo::EntityLifetimeUndo::capture(undo::EntityLifetimeUndo::Kind::Created, duplicate));
                recorder.push();
                return ToolResult::ok({{"id", entityId(duplicate)}, {"source", entityId(entity.handle)}});
            };
            registry.add(std::move(tool));
        }

        void registerEntitySetParent(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "entity_set_parent";
            tool.title = "Reparent entity";
            tool.description =
                "Move an entity (with its subtree) under a new parent, appended as the last child. Omit 'parent' "
                "(or pass null) to move it to the scene root. The LOCAL transform values are kept as-is, so the "
                "world placement changes if the new parent is transformed. Terrains and terrain tiles can be neither "
                "moved nor used as the parent. Undoable (undo restores the old sibling position).";
            tool.inputSchema = schema::object({
                {"entity", schema::entity()},
                {"parent", schema::entity("New parent entity id. Omit or null for the scene root.")}
            }, {"entity"});
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                services::EntityData entity = requireEntity(reader, "entity");
                rejectRoot(entity, "reparent");
                rejectTerrain(entity, "reparent", moveTerrainHint);

                services::EntityHandle parent;
                if (reader.has("parent"))
                {
                    const services::EntityData parentData = requireEntity(reader, "parent");
                    rejectTerrain(parentData, "reparent an entity under", moveTerrainHint);
                    parent = parentData.handle;
                }
                else
                {
                    parent = sceneRoot();
                }
                if (parent == entity.handle)
                {
                    throw ArgError("an entity cannot be its own parent");
                }

                // Record the old slot before the move (rejectRoot guarantees a parent).
                const services::EntityHandle oldParent = entity.parent.value_or(sceneRoot());
                const int oldIndex = childIndex(oldParent, entity.handle).value_or(-1);

                events::scene::ReparentEntityCommand command;
                command.entity = entity.handle;
                command.newParent = parent;
                if (!events::EventDispatcher::instance().execute(command))
                {
                    return ToolResult::error("Reparent failed (the new parent may be a descendant of the entity)");
                }

                undo::UndoRecorder recorder("MCP: Reparent (" + entity.name + ")");
                recorder.add(std::make_unique<undo::ReparentUndo>(toId(entity.handle), entity.name, toId(oldParent),
                                                                  oldIndex, toId(parent)));
                recorder.push();
                return ToolResult::ok({{"id", entityId(entity.handle)}, {"parent", entityId(parent)}});
            };
            registry.add(std::move(tool));
        }

        void registerEntityRename(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "entity_rename";
            tool.title = "Rename entity";
            tool.description = "Set an entity's name. Undoable.";
            tool.inputSchema = schema::object({
                {"entity", schema::entity()},
                {"name", schema::string("New name")}
            }, {"entity", "name"});
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                services::EntityData entity = requireEntity(reader, "entity");
                std::string name = reader.requireString("name");
                if (name.empty())
                {
                    throw ArgError("argument 'name' must not be empty");
                }

                events::scene::SetEntityNameCommand command;
                command.entity = entity.handle;
                command.newName = name;
                events::EventDispatcher::instance().execute(command);

                undo::UndoRecorder recorder("MCP: Rename (" + entity.name + " -> " + name + ")");
                recorder.add(std::make_unique<undo::RenameUndo>(toId(entity.handle), entity.name, name));
                recorder.push();
                return ToolResult::ok({{"id", entityId(entity.handle)}, {"name", name}});
            };
            registry.add(std::move(tool));
        }

        void registerEntitySetActive(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "entity_set_active";
            tool.title = "Activate / deactivate entity";
            tool.description = "Enable or disable an entity. A disabled entity (and its subtree) is not rendered or simulated. Undoable.";
            tool.inputSchema = schema::object({
                {"entity", schema::entity()},
                {"active", schema::boolean("true = enabled")}
            }, {"entity", "active"});
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                services::EntityData entity = requireEntity(reader, "entity");

                events::scene::SetEntityActiveCommand command;
                command.entity = entity.handle;
                command.isActive = reader.requireBool("active");
                events::EventDispatcher::instance().execute(command);

                undo::UndoRecorder recorder(std::string("MCP: ") + (command.isActive ? "Activate" : "Deactivate") +
                                            " (" + entity.name + ")");
                recorder.add(std::make_unique<undo::ActiveUndo>(toId(entity.handle), entity.name, entity.isActive,
                                                                command.isActive));
                recorder.push();
                return ToolResult::ok({{"id", entityId(entity.handle)}, {"active", command.isActive}});
            };
            registry.add(std::move(tool));
        }

        void registerEntitySelect(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "entity_select";
            tool.title = "Select entity";
            tool.description = "Select an entity in the editor (inspector + gizmo follow it). Omit 'entity' to clear the selection.";
            tool.inputSchema = schema::object({
                {"entity", schema::entity("Entity to select. Omit or null to clear the selection.")}
            });
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                events::scene::SelectEntityCommand command;
                if (reader.has("entity"))
                {
                    command.entity = requireEntity(reader, "entity").handle;
                }
                events::EventDispatcher::instance().execute(command);
                return ToolResult::ok({{"selected", entityId(command.entity)}});
            };
            registry.add(std::move(tool));
        }

        void registerEntitySetTransform(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "entity_set_transform";
            tool.title = "Set entity transform";
            tool.description =
                "Partially update an entity's LOCAL transform: only the provided fields change. "
                "rotationEuler is in degrees. Refused for terrains and terrain tiles. Undoable. Returns the resulting "
                "local transform.";
            nlohmann::json properties = transformProperties();
            properties["entity"] = schema::entity();
            tool.inputSchema = schema::object(std::move(properties), {"entity"});
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                services::EntityData entity = requireEntity(reader, "entity");
                rejectTerrain(entity, "transform", transformTerrainHint);
                if (!hasTransformArgs(reader))
                {
                    throw ArgError("provide at least one of 'position', 'rotationEuler', 'scale'");
                }
                const TransformEdit edit = applyTransform(reader, entity.handle);

                undo::UndoRecorder recorder("MCP: Set transform (" + entity.name + ")");
                recorder.add(std::make_unique<undo::TransformUndo>(toId(entity.handle), entity.name, edit.before,
                                                                   edit.after));
                recorder.push();

                nlohmann::json out = transformToJson(edit.after);
                out["id"] = entityId(entity.handle);
                return ToolResult::ok(std::move(out));
            };
            registry.add(std::move(tool));
        }

        void registerEntityGet(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "entity_get";
            tool.title = "Get entity";
            tool.description =
                "Full description of an entity: 'entity' = summary {id, name, active, parent, children, "
                "transform (local), worldTransform}; 'json' = the entity subtree serialized in prefab format "
                "(every component with all fields). The 'json' value can be edited and fed to "
                "entity_instantiate_json to create a modified copy.";
            tool.inputSchema = schema::object({{"entity", schema::entity()}}, {"entity"});
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                services::EntityData entity = requireEntity(reader, "entity");

                nlohmann::json out{{"entity", entitySummary(entity)}};

                events::scene::CopyEntityToJsonQuery query;
                query.entity = entity.handle;
                std::string text = events::EventDispatcher::instance().query(query);
                if (text.empty())
                {
                    out["json"] = nullptr;  // the scene root is not serializable as a prefab
                }
                else
                {
                    nlohmann::json parsed = nlohmann::json::parse(text, nullptr, false);
                    if (parsed.is_discarded())
                    {
                        return ToolResult::error("CopyEntityToJson returned malformed JSON");
                    }
                    out["json"] = std::move(parsed);
                }
                return ToolResult::ok(std::move(out));
            };
            registry.add(std::move(tool));
        }

        void registerEntityInstantiateJson(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "entity_instantiate_json";
            tool.title = "Instantiate entity from JSON";
            tool.description =
                "Create an entity subtree from prefab-format JSON (the 'json' value returned by entity_get), "
                "under 'parent' (default: scene root). Fresh ids/UUIDs are assigned and referenced assets are "
                "loaded. Use this for components that have no dedicated tool. Undoable. Returns the new root id.";
            tool.inputSchema = schema::object({
                {"json", {{"type", {"object", "string"}}, {"description", "Prefab-format JSON, as an object or as a JSON string"}}},
                {"parent", schema::entity("Parent entity id. Omit for the scene root.")}
            }, {"json"});
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                const nlohmann::json& source = reader.raw("json");

                events::scene::InstantiateEntityFromJsonCommand command;
                if (source.is_string())
                {
                    command.jsonText = source.get<std::string>();
                    if (nlohmann::json::parse(command.jsonText, nullptr, false).is_discarded())
                    {
                        throw ArgError("argument 'json' is not valid JSON");
                    }
                }
                else if (source.is_object())
                {
                    command.jsonText = source.dump();
                }
                else
                {
                    throw ArgError("argument 'json' must be an object or a JSON string");
                }

                if (reader.has("parent"))
                {
                    command.parent = requireEntity(reader, "parent").handle;
                }

                auto created = events::EventDispatcher::instance().execute(command);
                if (!created.has_value() || !created->isValid())
                {
                    return ToolResult::error("InstantiateEntityFromJson failed (see logs_read for the parse error)");
                }

                undo::UndoRecorder recorder("MCP: Instantiate entity from JSON");
                recorder.add(undo::EntityLifetimeUndo::capture(undo::EntityLifetimeUndo::Kind::Created, *created));
                recorder.push();
                return ToolResult::ok({{"id", entityId(*created)}, {"parent", entityId(command.parent)}});
            };
            registry.add(std::move(tool));
        }
    }

    void registerEntityTools(ToolRegistry& registry, const ToolContext&)
    {
        registerEntityFind(registry);
        registerEntityCreate(registry);
        registerEntityDelete(registry);
        registerEntityDuplicate(registry);
        registerEntitySetParent(registry);
        registerEntityRename(registry);
        registerEntitySetActive(registry);
        registerEntitySelect(registry);
        registerEntitySetTransform(registry);
        registerEntityGet(registry);
        registerEntityInstantiateJson(registry);
    }
}

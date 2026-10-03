#include "McpUndoCommands.hpp"
#include "EntityIdRemap.hpp"
#include "../tools/ToolHelpers.hpp"

#include "events/EventDispatcher.hpp"
#include "events/render/MaterialEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/scene/PluginComponentEvents.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"

#include <stdexcept>
#include <utility>

namespace mcp::undo
{
    namespace
    {
        size_t jsonBytes(const nlohmann::json& value)
        {
            return value.dump().size();
        }

        std::string idText(uint32_t recordedId)
        {
            return std::to_string(recordedId);
        }

        services::EntityData requireLive(uint32_t recordedId)
        {
            auto data = tools::findEntity(currentHandle(recordedId));
            if (!data.has_value())
            {
                throw std::runtime_error("entity " + idText(recordedId) + " no longer exists");
            }
            return *data;
        }

        void checkPluginResult(const events::scene::PluginComponentResult& result, const std::string& action)
        {
            if (!result.ok)
            {
                throw std::runtime_error(action + " failed: " + result.error);
            }
        }

        EntityIdNode buildIdTree(const services::EntityData& entity, const nlohmann::json& entityJson)
        {
            EntityIdNode node;
            node.id = tools::toId(entity.handle);
            node.name = entity.name;

            auto childrenJson = entityJson.find("children");
            if (childrenJson == entityJson.end() || !childrenJson->is_array() || childrenJson->empty())
            {
                return node;
            }

            std::vector<std::string> expected;
            for (const nlohmann::json& child : *childrenJson)
            {
                expected.push_back(child.is_object() ? child.value("name", std::string{}) : std::string{});
            }

            std::vector<services::EntityData> liveChildren;
            std::vector<std::string> actual;
            for (const services::EntityHandle& child : entity.children)
            {
                if (auto data = tools::findEntity(child))
                {
                    actual.push_back(data->name);
                    liveChildren.push_back(std::move(*data));
                }
            }

            const std::vector<int> match = alignChildren(expected, actual);
            for (std::size_t i = 0; i < match.size(); ++i)
            {
                if (match[i] < 0)
                {
                    // Keep the slot so later siblings stay aligned with the JSON.
                    node.children.push_back(EntityIdNode{0, false, expected[i], {}});
                    continue;
                }
                node.children.push_back(buildIdTree(liveChildren[static_cast<std::size_t>(match[i])], (*childrenJson)[i]));
            }
            return node;
        }

        // Walks the recorded tree and the freshly instantiated one in parallel.
        void recordSubtree(const EntityIdNode& recorded, const services::EntityHandle& created)
        {
            if (recorded.live)
            {
                EntityIdRemap::instance().record(recorded.id, tools::toId(created));
            }
            if (recorded.children.empty())
            {
                return;
            }

            auto data = tools::findEntity(created);
            if (!data.has_value())
            {
                return;
            }

            std::vector<std::string> expected;
            for (const EntityIdNode& child : recorded.children)
            {
                expected.push_back(child.name);
            }
            std::vector<services::EntityHandle> handles;
            std::vector<std::string> actual;
            for (const services::EntityHandle& child : data->children)
            {
                if (auto childData = tools::findEntity(child))
                {
                    handles.push_back(child);
                    actual.push_back(childData->name);
                }
            }

            const std::vector<int> match = alignChildren(expected, actual);
            for (std::size_t i = 0; i < match.size(); ++i)
            {
                if (match[i] >= 0)
                {
                    recordSubtree(recorded.children[i], handles[static_cast<std::size_t>(match[i])]);
                }
            }
        }

        size_t treeBytes(const EntityIdNode& node)
        {
            size_t total = sizeof(EntityIdNode) + node.name.size();
            for (const EntityIdNode& child : node.children)
            {
                total += treeBytes(child);
            }
            return total;
        }
    }

    services::EntityHandle currentHandle(uint32_t recordedId)
    {
        return tools::toHandle(EntityIdRemap::instance().resolve(recordedId));
    }

    int reorderInsertIndex(std::optional<int> currentIndex, int desiredIndex)
    {
        // Same parent, entity in front of the slot: moveEntity decrements the target,
        // so ask for one more. desiredIndex <= currentIndex needs no correction.
        if (currentIndex.has_value() && *currentIndex < desiredIndex)
        {
            return desiredIndex + 1;
        }
        return desiredIndex;
    }

    std::vector<int> alignChildren(const std::vector<std::string>& expected, const std::vector<std::string>& actual)
    {
        std::vector<int> match(expected.size(), -1);
        if (expected.size() == actual.size())
        {
            for (std::size_t i = 0; i < expected.size(); ++i)
            {
                match[i] = static_cast<int>(i);
            }
            return match;
        }

        std::size_t next = 0;
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            std::size_t candidate = next;
            while (candidate < actual.size() && actual[candidate] != expected[i])
            {
                ++candidate;
            }
            if (candidate < actual.size())
            {
                match[i] = static_cast<int>(candidate);
                next = candidate + 1;
            }
        }
        return match;
    }

    // ------------------------------------------------------------------
    // TransformUndo
    // ------------------------------------------------------------------

    TransformUndo::TransformUndo(uint32_t entity, std::string entityName, services::TransformData before,
                                 services::TransformData after)
        : entity(entity), entityName(std::move(entityName)), before(before), after(after)
    {
    }

    void TransformUndo::apply(const services::TransformData& transform) const
    {
        requireLive(entity);
        events::scene::SetTransformCommand command;
        command.entity = currentHandle(entity);
        command.transform = transform;
        events::EventDispatcher::instance().execute(command);
    }

    void TransformUndo::execute() { apply(after); }
    void TransformUndo::undo() { apply(before); }

    std::string TransformUndo::getDescription() const
    {
        return "MCP: Set transform (" + entityName + ")";
    }

    size_t TransformUndo::getMemoryFootprint() const
    {
        return sizeof(*this) + entityName.size();
    }

    // ------------------------------------------------------------------
    // RenameUndo
    // ------------------------------------------------------------------

    RenameUndo::RenameUndo(uint32_t entity, std::string before, std::string after)
        : entity(entity), before(std::move(before)), after(std::move(after))
    {
    }

    void RenameUndo::apply(const std::string& name) const
    {
        requireLive(entity);
        events::scene::SetEntityNameCommand command;
        command.entity = currentHandle(entity);
        command.newName = name;
        events::EventDispatcher::instance().execute(command);
    }

    void RenameUndo::execute() { apply(after); }
    void RenameUndo::undo() { apply(before); }

    std::string RenameUndo::getDescription() const
    {
        return "MCP: Rename (" + before + " -> " + after + ")";
    }

    size_t RenameUndo::getMemoryFootprint() const
    {
        return sizeof(*this) + before.size() + after.size();
    }

    // ------------------------------------------------------------------
    // ActiveUndo
    // ------------------------------------------------------------------

    ActiveUndo::ActiveUndo(uint32_t entity, std::string entityName, bool before, bool after)
        : entity(entity), entityName(std::move(entityName)), before(before), after(after)
    {
    }

    void ActiveUndo::apply(bool active) const
    {
        requireLive(entity);
        events::scene::SetEntityActiveCommand command;
        command.entity = currentHandle(entity);
        command.isActive = active;
        events::EventDispatcher::instance().execute(command);
    }

    void ActiveUndo::execute() { apply(after); }
    void ActiveUndo::undo() { apply(before); }

    std::string ActiveUndo::getDescription() const
    {
        return std::string("MCP: ") + (after ? "Activate" : "Deactivate") + " (" + entityName + ")";
    }

    size_t ActiveUndo::getMemoryFootprint() const
    {
        return sizeof(*this) + entityName.size();
    }

    // ------------------------------------------------------------------
    // ReparentUndo
    // ------------------------------------------------------------------

    ReparentUndo::ReparentUndo(uint32_t entity, std::string entityName, uint32_t oldParent, int oldIndex,
                               uint32_t newParent)
        : entity(entity), entityName(std::move(entityName)), oldParent(oldParent), oldIndex(oldIndex),
          newParent(newParent)
    {
    }

    void ReparentUndo::execute()
    {
        requireLive(entity);
        events::scene::ReparentEntityCommand command;
        command.entity = currentHandle(entity);
        command.newParent = currentHandle(newParent);
        if (!events::EventDispatcher::instance().execute(command))
        {
            throw std::runtime_error("reparenting entity " + idText(entity) + " failed");
        }
    }

    void ReparentUndo::undo()
    {
        const services::EntityData data = requireLive(entity);
        const services::EntityHandle target = currentHandle(oldParent);

        // Re-read the live position: other edits may have moved siblings since.
        std::optional<int> currentIndex;
        if (data.parent.has_value() && *data.parent == target)
        {
            currentIndex = tools::childIndex(target, data.handle);
        }

        events::scene::ReorderEntityCommand command;
        command.entity = data.handle;
        command.newParent = target;
        command.insertIndex = oldIndex < 0 ? -1 : reorderInsertIndex(currentIndex, oldIndex);
        if (!events::EventDispatcher::instance().execute(command))
        {
            throw std::runtime_error("moving entity " + idText(entity) + " back to its old parent failed");
        }
    }

    std::string ReparentUndo::getDescription() const
    {
        return "MCP: Reparent (" + entityName + ")";
    }

    size_t ReparentUndo::getMemoryFootprint() const
    {
        return sizeof(*this) + entityName.size();
    }

    // ------------------------------------------------------------------
    // BuiltinComponentPatchUndo
    // ------------------------------------------------------------------

    BuiltinComponentPatchUndo::BuiltinComponentPatchUndo(uint32_t entity, std::string type, nlohmann::json before,
                                                         nlohmann::json after)
        : entity(entity), type(std::move(type)), before(std::move(before)), after(std::move(after))
    {
        footprint = sizeof(*this) + this->type.size() + jsonBytes(this->before) + jsonBytes(this->after);
    }

    void BuiltinComponentPatchUndo::apply(const nlohmann::json& fields) const
    {
        if (!tools::patchBuiltinComponent(type, currentHandle(entity), fields).has_value())
        {
            throw std::runtime_error("entity " + idText(entity) + " has no " + type + " component");
        }
    }

    void BuiltinComponentPatchUndo::execute() { apply(after); }
    void BuiltinComponentPatchUndo::undo() { apply(before); }

    std::string BuiltinComponentPatchUndo::getDescription() const
    {
        return "MCP: Set " + type + " fields";
    }

    size_t BuiltinComponentPatchUndo::getMemoryFootprint() const
    {
        return footprint;
    }

    // ------------------------------------------------------------------
    // BuiltinComponentPresenceUndo
    // ------------------------------------------------------------------

    BuiltinComponentPresenceUndo::BuiltinComponentPresenceUndo(uint32_t entity, std::string type,
                                                               nlohmann::json fields, bool added)
        : entity(entity), type(std::move(type)), fields(std::move(fields)), added(added)
    {
        footprint = sizeof(*this) + this->type.size() + jsonBytes(this->fields);
    }

    void BuiltinComponentPresenceUndo::restore() const
    {
        const services::EntityHandle handle = currentHandle(entity);
        if (!tools::addBuiltinComponent(type, handle))
        {
            throw std::runtime_error("adding " + type + " to entity " + idText(entity) + " failed");
        }
        if (fields.is_object() && !fields.empty())
        {
            tools::patchBuiltinComponent(type, handle, fields);
        }
    }

    void BuiltinComponentPresenceUndo::remove() const
    {
        if (!tools::removeBuiltinComponent(type, currentHandle(entity)))
        {
            throw std::runtime_error("removing " + type + " from entity " + idText(entity) + " failed");
        }
    }

    void BuiltinComponentPresenceUndo::execute()
    {
        added ? restore() : remove();
    }

    void BuiltinComponentPresenceUndo::undo()
    {
        added ? remove() : restore();
    }

    std::string BuiltinComponentPresenceUndo::getDescription() const
    {
        return std::string("MCP: ") + (added ? "Add " : "Remove ") + type + " component";
    }

    size_t BuiltinComponentPresenceUndo::getMemoryFootprint() const
    {
        return footprint;
    }

    // ------------------------------------------------------------------
    // PluginComponentPatchUndo
    // ------------------------------------------------------------------

    PluginComponentPatchUndo::PluginComponentPatchUndo(uint32_t entity, std::string type, nlohmann::json before,
                                                       nlohmann::json after)
        : entity(entity), type(std::move(type)), before(std::move(before)), after(std::move(after))
    {
        footprint = sizeof(*this) + this->type.size() + jsonBytes(this->before) + jsonBytes(this->after);
    }

    void PluginComponentPatchUndo::apply(const nlohmann::json& fields) const
    {
        events::scene::SetPluginComponentFieldsCommand command;
        command.entity = currentHandle(entity);
        command.type = type;
        command.fields = fields;
        checkPluginResult(events::EventDispatcher::instance().execute(command), "Setting " + type + " fields");
    }

    void PluginComponentPatchUndo::execute() { apply(after); }
    void PluginComponentPatchUndo::undo() { apply(before); }

    std::string PluginComponentPatchUndo::getDescription() const
    {
        return "MCP: Set " + type + " fields";
    }

    size_t PluginComponentPatchUndo::getMemoryFootprint() const
    {
        return footprint;
    }

    // ------------------------------------------------------------------
    // PluginComponentPresenceUndo
    // ------------------------------------------------------------------

    PluginComponentPresenceUndo::PluginComponentPresenceUndo(uint32_t entity, std::string type,
                                                             nlohmann::json fields, bool added)
        : entity(entity), type(std::move(type)), fields(std::move(fields)), added(added)
    {
        footprint = sizeof(*this) + this->type.size() + jsonBytes(this->fields);
    }

    void PluginComponentPresenceUndo::restore() const
    {
        events::scene::AddPluginComponentCommand command;
        command.entity = currentHandle(entity);
        command.type = type;
        command.fields = fields;
        checkPluginResult(events::EventDispatcher::instance().execute(command), "Adding " + type);
    }

    void PluginComponentPresenceUndo::remove() const
    {
        events::scene::RemovePluginComponentCommand command;
        command.entity = currentHandle(entity);
        command.type = type;
        checkPluginResult(events::EventDispatcher::instance().execute(command), "Removing " + type);
    }

    void PluginComponentPresenceUndo::execute()
    {
        added ? restore() : remove();
    }

    void PluginComponentPresenceUndo::undo()
    {
        added ? remove() : restore();
    }

    std::string PluginComponentPresenceUndo::getDescription() const
    {
        return std::string("MCP: ") + (added ? "Add " : "Remove ") + type + " component";
    }

    size_t PluginComponentPresenceUndo::getMemoryFootprint() const
    {
        return footprint;
    }

    nlohmann::json writablePluginFields(const std::string& type, const nlohmann::json& value)
    {
        if (!value.is_object())
        {
            return value;
        }
        const nlohmann::json types =
            events::EventDispatcher::instance().query(events::scene::GetPluginComponentTypesQuery{});
        if (!types.is_array())
        {
            return value;
        }
        for (const nlohmann::json& entry : types)
        {
            if (!entry.is_object() || entry.value("name", std::string{}) != type)
            {
                continue;
            }
            auto fields = entry.find("fields");
            if (fields == entry.end() || !fields->is_array())
            {
                return value;
            }
            nlohmann::json writable = nlohmann::json::object();
            for (const nlohmann::json& field : *fields)
            {
                if (!field.is_object() || field.value("readOnly", false))
                {
                    continue;
                }
                const std::string name = field.value("name", std::string{});
                auto current = value.find(name);
                if (!name.empty() && current != value.end())
                {
                    writable[name] = *current;
                }
            }
            return writable;
        }
        return value;
    }

    // ------------------------------------------------------------------
    // MaterialAssignUndo
    // ------------------------------------------------------------------

    MaterialAssignUndo::MaterialAssignUndo(uint32_t entity, std::optional<std::string> subMesh, bool hadComponent,
                                           std::string beforePath, std::string afterPath)
        : entity(entity), subMesh(std::move(subMesh)), hadComponent(hadComponent), beforePath(std::move(beforePath)),
          afterPath(std::move(afterPath))
    {
    }

    void MaterialAssignUndo::apply(const std::string& path) const
    {
        auto& dispatcher = events::EventDispatcher::instance();
        bool applied = false;
        if (subMesh.has_value())
        {
            events::material::SetSubMeshMaterialCommand command;
            command.entity = currentHandle(entity);
            command.submeshName = *subMesh;
            command.materialPath = path;
            applied = dispatcher.execute(command);
        }
        else
        {
            events::material::SetDefaultMaterialCommand command;
            command.entity = currentHandle(entity);
            command.materialPath = path;
            applied = dispatcher.execute(command);
        }
        if (!applied)
        {
            throw std::runtime_error("material assignment on entity " + idText(entity) + " was rejected");
        }
    }

    void MaterialAssignUndo::execute()
    {
        apply(afterPath);
    }

    void MaterialAssignUndo::undo()
    {
        if (!hadComponent)
        {
            events::material::RemoveMaterialComponentCommand command;
            command.entity = currentHandle(entity);
            if (!events::EventDispatcher::instance().execute(command))
            {
                throw std::runtime_error("removing the material component from entity " + idText(entity) + " failed");
            }
            return;
        }
        apply(beforePath);
    }

    std::string MaterialAssignUndo::getDescription() const
    {
        return subMesh.has_value() ? "MCP: Assign material (" + *subMesh + ")" : "MCP: Assign material";
    }

    size_t MaterialAssignUndo::getMemoryFootprint() const
    {
        return sizeof(*this) + (subMesh.has_value() ? subMesh->size() : 0) + beforePath.size() + afterPath.size();
    }

    // ------------------------------------------------------------------
    // EntityLifetimeUndo
    // ------------------------------------------------------------------

    std::unique_ptr<EntityLifetimeUndo> EntityLifetimeUndo::capture(Kind kind, const services::EntityHandle& entity)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        auto data = tools::findEntity(entity);
        if (!data.has_value())
        {
            throw std::runtime_error("entity " + std::to_string(tools::toId(entity)) + " does not exist");
        }
        if (!data->parent.has_value() || !data->parent->isValid())
        {
            throw std::runtime_error("the scene root cannot be recorded for undo");
        }

        events::scene::CopyEntityToJsonQuery query;
        query.entity = entity;
        std::string snapshot = dispatcher.query(query);
        nlohmann::json parsed = nlohmann::json::parse(snapshot, nullptr, false);
        if (snapshot.empty() || parsed.is_discarded() || !parsed.is_object())
        {
            throw std::runtime_error("entity " + std::to_string(tools::toId(entity)) + " could not be serialized for undo");
        }

        std::unique_ptr<EntityLifetimeUndo> command(new EntityLifetimeUndo());
        command->kind = kind;
        command->parent = tools::toId(*data->parent);
        command->siblingIndex = tools::childIndex(*data->parent, entity).value_or(-1);
        command->idTree = buildIdTree(*data, parsed);
        command->snapshotJson = std::move(snapshot);
        command->footprint = sizeof(EntityLifetimeUndo) + command->snapshotJson.size() + treeBytes(command->idTree);
        return command;
    }

    void EntityLifetimeUndo::instantiate() const
    {
        auto& dispatcher = events::EventDispatcher::instance();
        const services::EntityHandle parentHandle = currentHandle(parent);

        events::scene::InstantiateEntityFromJsonCommand command;
        command.jsonText = snapshotJson;
        command.parent = parentHandle;
        std::optional<services::EntityHandle> created = dispatcher.execute(command);
        if (!created.has_value() || !created->isValid())
        {
            throw std::runtime_error("re-creating entity '" + idTree.name + "' failed");
        }

        // The instantiate appended it; put it back in its old slot. Skipped when the
        // parent vanished and the engine fell back to the scene root.
        auto data = tools::findEntity(*created);
        if (siblingIndex >= 0 && data.has_value() && data->parent.has_value() && *data->parent == parentHandle)
        {
            events::scene::ReorderEntityCommand reorder;
            reorder.entity = *created;
            reorder.newParent = parentHandle;
            reorder.insertIndex = reorderInsertIndex(tools::childIndex(parentHandle, *created), siblingIndex);
            dispatcher.execute(reorder);
        }

        recordSubtree(idTree, *created);
    }

    void EntityLifetimeUndo::destroy() const
    {
        events::scene::DeleteEntityCommand command;
        command.entity = currentHandle(idTree.id);
        if (!events::EventDispatcher::instance().execute(command))
        {
            throw std::runtime_error("deleting entity '" + idTree.name + "' (" + idText(idTree.id) + ") failed");
        }
    }

    void EntityLifetimeUndo::execute()
    {
        kind == Kind::Created ? instantiate() : destroy();
    }

    void EntityLifetimeUndo::undo()
    {
        kind == Kind::Created ? destroy() : instantiate();
    }

    std::string EntityLifetimeUndo::getDescription() const
    {
        return std::string("MCP: ") + (kind == Kind::Created ? "Create" : "Delete") + " entity (" + idTree.name + ")";
    }

    size_t EntityLifetimeUndo::getMemoryFootprint() const
    {
        return footprint;
    }
}

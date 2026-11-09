#pragma once

#include "data/DTOs.hpp"
#include "data/EntityHandle.hpp"
#include "data/UndoTypes.hpp"

#include <nlohmann/json.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// VK-1651: undo entries for agent edits. Every command stores the entity ids it
// saw when it was recorded and resolves them through EntityIdRemap when it runs,
// so it keeps working after an undone delete re-minted the ids. Each one replays
// the same CQRS commands the tools use; nothing here touches EnTT directly.
// Failures throw; the McpBatchUndoCommand (McpUndo.hpp) that tools push catches,
// logs a warning and lets the entry be consumed, so a stale entry never blocks
// older undo history.
namespace mcp::undo
{
    // The live handle for an id recorded earlier.
    services::EntityHandle currentHandle(uint32_t recordedId);

    // ReorderEntityCommand's insertIndex refers to the child list BEFORE the move,
    // and a same-parent move shifts the target down by one when the entity sits in
    // front of it (SceneGraphSystem::moveEntity). Returns the insertIndex that lands
    // the entity at `desiredIndex`; `currentIndex` is the entity's index when it is
    // already under the target parent, nullopt when it comes from another parent.
    int reorderInsertIndex(std::optional<int> currentIndex, int desiredIndex);

    // Pairs each expected child name with an actual child, preserving order. Equal
    // counts zip by position; otherwise each expected name takes the next actual
    // child with that name (engine-managed UIListItem children are not serialized
    // into prefab JSON, so they appear only on the live side). -1 = no match.
    std::vector<int> alignChildren(const std::vector<std::string>& expected, const std::vector<std::string>& actual);

    class TransformUndo : public services::IUndoableCommand
    {
    public:
        TransformUndo(uint32_t entity, std::string entityName, services::TransformData before,
                      services::TransformData after);

        void execute() override;
        void undo() override;
        std::string getDescription() const override;
        size_t getMemoryFootprint() const override;

    private:
        void apply(const services::TransformData& transform) const;

        uint32_t entity;
        std::string entityName;
        services::TransformData before;
        services::TransformData after;
    };

    class RenameUndo : public services::IUndoableCommand
    {
    public:
        RenameUndo(uint32_t entity, std::string before, std::string after);

        void execute() override;
        void undo() override;
        std::string getDescription() const override;
        size_t getMemoryFootprint() const override;

    private:
        void apply(const std::string& name) const;

        uint32_t entity;
        std::string before;
        std::string after;
    };

    class ActiveUndo : public services::IUndoableCommand
    {
    public:
        ActiveUndo(uint32_t entity, std::string entityName, bool before, bool after);

        void execute() override;
        void undo() override;
        std::string getDescription() const override;
        size_t getMemoryFootprint() const override;

    private:
        void apply(bool active) const;

        uint32_t entity;
        std::string entityName;
        bool before;
        bool after;
    };

    // entity_set_parent: redo appends under newParent (ReparentEntityCommand, as the
    // tool did); undo puts the entity back at oldIndex under oldParent.
    class ReparentUndo : public services::IUndoableCommand
    {
    public:
        ReparentUndo(uint32_t entity, std::string entityName, uint32_t oldParent, int oldIndex, uint32_t newParent);

        void execute() override;
        void undo() override;
        std::string getDescription() const override;
        size_t getMemoryFootprint() const override;

    private:
        uint32_t entity;
        std::string entityName;
        uint32_t oldParent;
        int oldIndex;
        uint32_t newParent;
    };

    // component_set on a curated type: re-applies the full before / after field
    // object through the ComponentTools binding.
    class BuiltinComponentPatchUndo : public services::IUndoableCommand
    {
    public:
        BuiltinComponentPatchUndo(uint32_t entity, std::string type, nlohmann::json before, nlohmann::json after);

        void execute() override;
        void undo() override;
        std::string getDescription() const override;
        size_t getMemoryFootprint() const override;

    private:
        void apply(const nlohmann::json& fields) const;

        uint32_t entity;
        std::string type;
        nlohmann::json before;
        nlohmann::json after;
        size_t footprint;
    };

    // component_add (added = true) / component_remove (added = false) on a curated
    // type. `fields` is the component's full field object: after the add, or just
    // before the remove. Restoring = add + patch(fields).
    class BuiltinComponentPresenceUndo : public services::IUndoableCommand
    {
    public:
        BuiltinComponentPresenceUndo(uint32_t entity, std::string type, nlohmann::json fields, bool added);

        void execute() override;
        void undo() override;
        std::string getDescription() const override;
        size_t getMemoryFootprint() const override;

    private:
        void restore() const;
        void remove() const;

        uint32_t entity;
        std::string type;
        nlohmann::json fields;
        bool added;
        size_t footprint;
    };

    // component_set_generic: `before` / `after` hold only the keys the agent set, so
    // the strict SetPluginComponentFieldsCommand never sees a read-only field.
    class PluginComponentPatchUndo : public services::IUndoableCommand
    {
    public:
        PluginComponentPatchUndo(uint32_t entity, std::string type, nlohmann::json before, nlohmann::json after);

        void execute() override;
        void undo() override;
        std::string getDescription() const override;
        size_t getMemoryFootprint() const override;

    private:
        void apply(const nlohmann::json& fields) const;

        uint32_t entity;
        std::string type;
        nlohmann::json before;
        nlohmann::json after;
        size_t footprint;
    };

    // component_add_generic (added = true) / component_remove_generic (added = false).
    // `fields` are writable fields only (see writablePluginFields): the agent's own
    // arguments for an add, the pre-removal value minus read-only fields for a remove.
    class PluginComponentPresenceUndo : public services::IUndoableCommand
    {
    public:
        PluginComponentPresenceUndo(uint32_t entity, std::string type, nlohmann::json fields, bool added);

        void execute() override;
        void undo() override;
        std::string getDescription() const override;
        size_t getMemoryFootprint() const override;

    private:
        void restore() const;
        void remove() const;

        uint32_t entity;
        std::string type;
        nlohmann::json fields;
        bool added;
        size_t footprint;
    };

    // Keeps the members of `value` that GetPluginComponentTypesQuery lists for `type`
    // as fields without readOnly:true. Unknown type metadata -> `value` unchanged.
    nlohmann::json writablePluginFields(const std::string& type, const nlohmann::json& value);

    // material_assign. subMesh nullopt = the default material. hadComponent=false means
    // the assignment added the MaterialComponent, so undo removes it again.
    class MaterialAssignUndo : public services::IUndoableCommand
    {
    public:
        MaterialAssignUndo(uint32_t entity, std::optional<std::string> subMesh, bool hadComponent,
                           std::string beforePath, std::string afterPath);

        void execute() override;
        void undo() override;
        std::string getDescription() const override;
        size_t getMemoryFootprint() const override;

    private:
        void apply(const std::string& path) const;

        uint32_t entity;
        std::optional<std::string> subMesh;
        bool hadComponent;
        std::string beforePath;
        std::string afterPath;
    };

    struct EntityIdNode
    {
        uint32_t id = 0;
        bool live = true;  // false: a JSON child with no live counterpart (slot kept for alignment)
        std::string name;
        std::vector<EntityIdNode> children;  // aligned with the snapshot JSON "children"
    };

    // An entity subtree coming into or going out of existence. Created: the edit made
    // it (entity_create / _duplicate / _instantiate_json) -> undo deletes, redo
    // re-instantiates. Deleted: the edit removed it (entity_delete) -> the inverse.
    // Re-instantiation restores the sibling index and records old -> new ids for the
    // whole subtree in EntityIdRemap.
    class EntityLifetimeUndo : public services::IUndoableCommand
    {
    public:
        enum class Kind
        {
            Created,
            Deleted
        };

        // Snapshots `entity` (prefab JSON, parent, sibling index, id tree). Call it
        // after a creation is complete, or before a delete. Throws when the entity is
        // gone, is the scene root, or cannot be serialized.
        static std::unique_ptr<EntityLifetimeUndo> capture(Kind kind, const services::EntityHandle& entity);

        void execute() override;
        void undo() override;
        std::string getDescription() const override;
        size_t getMemoryFootprint() const override;

        uint32_t rootId() const { return idTree.id; }

    private:
        EntityLifetimeUndo() = default;

        void instantiate() const;
        void destroy() const;

        Kind kind = Kind::Created;
        std::string snapshotJson;
        uint32_t parent = 0;
        int siblingIndex = -1;
        EntityIdNode idTree;
        size_t footprint = 0;
    };
}

#pragma once

#include "../tools/WaterToolSupport.hpp"

#include "data/DTOs.hpp"
#include "data/EntityHandle.hpp"
#include "data/OceanData.hpp"
#include "data/UndoTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

// VK-1658: undo entries for the water tools. Each replays the same Services events the tools use
// (Get/SetOceanState, CreateOcean, DeleteEntity, SetWaterBodyData) and stores the entity ids it saw when it
// was recorded, resolving them through EntityIdRemap when it runs. Failures throw; the McpBatchUndoCommand
// (McpUndo.hpp) the tools push logs them and lets the entry be consumed. Water bodies otherwise reuse the
// generic EntityLifetimeUndo (the prefab snapshot carries the WaterBodyComponent), TransformUndo and
// RenameUndo; the ocean cannot, because the prefab snapshot has no ocean.
namespace mcp::undo
{
    // A change of the scene's ocean. Applying a side re-reads the ocean's current state and copies only
    // `parts` from it, so undoing this entry never reverts what changed elsewhere since (the Ocean Editor,
    // another tool call on other parts). Throws when the recorded ocean is no longer the scene's ocean.
    class OceanStateUndo : public services::IUndoableCommand
    {
    public:
        OceanStateUndo(uint32_t ocean, services::OceanStateData before, services::OceanStateData after,
                       tools::watertool::OceanParts parts, std::string description);

        void execute() override;
        void undo() override;
        std::string getDescription() const override;
        size_t getMemoryFootprint() const override;

    private:
        void apply(const services::OceanStateData& source) const;

        uint32_t ocean;
        services::OceanStateData before;
        services::OceanStateData after;
        tools::watertool::OceanParts parts;
        std::string description;
    };

    // The ocean coming into or going out of existence. Created: the edit made it (ocean_enable, the hidden
    // ocean water_set_params creates) -> undo deletes it, redo re-creates it. Deleted: ocean_enable
    // {remove:true} -> the inverse. Re-creation runs CreateOceanCommand + SetOceanStateCommand with the
    // recorded state, restores the name, parent and sibling slot, and records old -> new id in
    // EntityIdRemap; it is refused while another ocean exists. Deleting is refused once the ocean has children
    // or other components (watertool::oceanExtras): re-creation could not bring them back.
    class OceanLifetimeUndo : public services::IUndoableCommand
    {
    public:
        enum class Kind
        {
            Created,
            Deleted
        };

        // Snapshots `ocean` (its OceanStateData, name, parent and sibling index). Call it after a creation is
        // complete, or before a delete. Throws when `ocean` is not the scene's ocean or is the scene root.
        static std::unique_ptr<OceanLifetimeUndo> capture(Kind kind, const services::EntityHandle& ocean);

        void execute() override;
        void undo() override;
        std::string getDescription() const override;
        size_t getMemoryFootprint() const override;

        uint32_t oceanId() const { return id; }

    private:
        OceanLifetimeUndo() = default;

        void instantiate() const;
        void destroy() const;

        Kind kind = Kind::Created;
        uint32_t id = 0;
        std::string name;
        uint32_t parent = 0;
        int siblingIndex = -1;
        services::OceanStateData state;
    };

    // water_set_body's component change (type, extents, depth, waves, physics, active), replayed through
    // SetWaterBodyDataCommand. Throws when the entity is gone or lost its WaterBodyComponent.
    class WaterBodyUndo : public services::IUndoableCommand
    {
    public:
        WaterBodyUndo(uint32_t entity, std::string entityName, services::WaterBodyComponentData before,
                      services::WaterBodyComponentData after);

        void execute() override;
        void undo() override;
        std::string getDescription() const override;
        size_t getMemoryFootprint() const override;

    private:
        void apply(const services::WaterBodyComponentData& data) const;

        uint32_t entity;
        std::string entityName;
        services::WaterBodyComponentData before;
        services::WaterBodyComponentData after;
    };
}

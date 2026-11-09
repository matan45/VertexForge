#pragma once

#include "../tools/UIToolSupport.hpp"

#include "data/UndoTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// VK-1659: the undo entry of a ui_set (and of the descendants a placement moves). Creation tools reuse the
// generic EntityLifetimeUndo (the prefab snapshot carries every UI component), renames RenameUndo and the
// active flag ActiveUndo. Replays the same Services events the tools use (Get/Set*Data, Add/Remove*Component)
// and resolves the entity id it recorded through EntityIdRemap when it runs. Failures throw; the
// McpBatchUndoCommand (McpUndo.hpp) the tools push logs them and lets the entry be consumed.
namespace mcp::undo
{
    struct UISectionChange
    {
        tools::uitool::Section section = tools::uitool::Section::Rect;
        // nullopt: the call attached the component (undo removes it again, redo re-adds it).
        std::optional<tools::uitool::SectionData> before;
        tools::uitool::SectionData after;
    };

    // One entity's section changes, in the order the call applied them. Undo walks them backwards: an
    // attached section is removed, any other is restored to `before`; redo re-adds attached sections and
    // applies `after`. Values are written with copyAuthored over the live DTO, so the engine's runtime state
    // (button state, an open dropdown, a progress bar's display value) is never rewound.
    class UISectionUndo : public services::IUndoableCommand
    {
    public:
        UISectionUndo(uint32_t entity, std::string entityName, std::vector<UISectionChange> changes);

        void execute() override;
        void undo() override;
        std::string getDescription() const override;
        size_t getMemoryFootprint() const override;

    private:
        uint32_t entity;
        std::string entityName;
        std::vector<UISectionChange> changes;
        size_t footprint = 0;
    };
}

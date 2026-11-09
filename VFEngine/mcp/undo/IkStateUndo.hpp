#pragma once

#include "animator/IKTypes.hpp"
#include "data/UndoTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// VK-1656: undo entry for animation_ik_set / animation_ik_remove (tools/AnimationIkTools.cpp).
namespace mcp::undo
{
    // One entity's IK component as a whole: present or not, and its chains in solve order.
    struct IkState
    {
        bool hasComponent = false;
        std::vector<animator::ik::IKChainConfig> chains;
    };

    // Replays a state exactly: AddIKComponent when the component is missing (it copies the chains
    // authored in the .vfMesh, which the following SetIKChains replaces) + SetIKChains, or
    // RemoveIKComponent. SetIKChains clears the runtime targets, as the original edit did. The entity is
    // the id seen when the edit was recorded, resolved through EntityIdRemap; a missing entity or a
    // refused command throws (McpBatchUndoCommand logs it).
    class IkStateUndo : public services::IUndoableCommand
    {
    public:
        IkStateUndo(uint32_t entity, std::string entityName, IkState before, IkState after);

        void execute() override;
        void undo() override;
        std::string getDescription() const override;
        size_t getMemoryFootprint() const override;

    private:
        void apply(const IkState& state) const;

        uint32_t entity;
        std::string entityName;
        IkState before;
        IkState after;
        size_t footprint;
    };
}

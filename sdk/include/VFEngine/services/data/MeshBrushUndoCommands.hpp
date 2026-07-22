#pragma once

#include "UndoTypes.hpp"
#include "../../utilities/meshbrush/MeshBrushTypes.hpp"

#include <string>
#include <vector>

namespace services
{
    class MeshBrushStrokeUndoCommand : public IUndoableCommand
    {
    private:
        std::vector<meshbrush::MeshBrushInstanceSpec> created;
        std::vector<meshbrush::MeshBrushInstanceSpec> removed;
        std::string description;

        void apply(const std::vector<meshbrush::MeshBrushInstanceSpec>& toRemove,
                   const std::vector<meshbrush::MeshBrushInstanceSpec>& toRespawn);

    public:
        MeshBrushStrokeUndoCommand(
            std::vector<meshbrush::MeshBrushInstanceSpec> createdSpecs,
            std::vector<meshbrush::MeshBrushInstanceSpec> removedSpecs);

        bool hasChanges() const;

        void execute() override;
        void undo() override;
        std::string getDescription() const override { return description; }
    };
}

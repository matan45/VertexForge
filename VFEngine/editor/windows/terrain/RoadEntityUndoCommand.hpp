#pragma once

#include "RoadMeshGenerator.hpp"
#include "data/UndoTypes.hpp"

#include <string>

namespace windows
{
    // VK-1621 — the scene half of undoing one road apply, pushed by RoadMeshGenerator into the
    // batch the spline service opens around the apply, so a single Ctrl+Z reverts the terrain
    // deformation, the painted weights AND the road entities.
    //
    // Undo deletes the road's entity subtree but deliberately leaves the generated .vfMesh on
    // disk, so redo respawns from the existing asset instead of regenerating the geometry. The
    // asset is small, regeneration is not free, and the entities are the only thing that changed.
    class RoadEntityUndoCommand : public services::IUndoableCommand
    {
    private:
        RoadMeshGenerator::RoadSpawnDesc desc;
        services::EntityHandle root;

    public:
        RoadEntityUndoCommand(RoadMeshGenerator::RoadSpawnDesc spawnDesc, services::EntityHandle rootEntity)
            : desc(std::move(spawnDesc)), root(rootEntity)
        {
        }

        void execute() override;
        void undo() override;

        std::string getDescription() const override;
        size_t getMemoryFootprint() const override;
    };
}

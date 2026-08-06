#pragma once

#include "UndoTypes.hpp"
#include "../events/terrain/HeightLayerUndoEvents.hpp"
#include "../events/terrain/SplineTerrainEvents.hpp"

#include <cstdint>
#include <string>

namespace services
{
    // VK-1648 — operation-level undo for the reserved height-layer stack.
    //
    // Every command here stores the OPERATION and its PARAMETERS, never a merged tile snapshot.
    // That is the whole premise of edit layers: composition always restarts from the authoritative
    // base, so replaying a definition is bit-identical on the interior no matter how many layers
    // overlap or in what order they are reverted. A snapshot would additionally write DERIVED data
    // that the next recompose silently discards.
    //
    // None of them holds a pointer to TerrainService or TerrainGrid. They reach the terrain only
    // through the non-recording primitives in SplineTerrainEvents.hpp and HeightLayerUndoEvents.hpp,
    // exactly as TerrainStrokeUndoCommand reaches it through RestoreStrokeStateCommand.
    //
    // DISPATCH NOTE, easy to get wrong and silent when you do: a command whose ResultType is not
    // void is registered with registerQueryHandler, so it must be invoked with dispatcher.query().
    // EventDispatcher::execute() searches only the command table and THROWS when it misses -- and
    // UndoRedoServiceImpl::undo() catches that, pushes the entry back and returns false, so the
    // symptom is "Ctrl+Z did nothing" rather than a crash. Only RemoveSplineHeightLayerCommand
    // (ICommand<>) goes through execute().

    // Add, delete, and the in-place update of an existing layer -- all three are "the layer looked
    // like THIS before and like THAT after", so one command covers them:
    //
    //   add     before.present == false, after.present == true
    //   delete  before.present == true,  after.present == false
    //   update  both present, different parameters
    //
    // An absent endpoint means "remove the layer"; a present one means "restore it exactly".
    //
    // Hide/show, reorder and rename get their own commands below rather than reusing this one:
    // storing two full snapshots -- each carrying a road's polyline and its affected-tile set -- in
    // order to flip a bool would put megabytes on the undo byte budget for no reason.
    class HeightLayerRecordUndoCommand : public IUndoableCommand
    {
    private:
        std::string description;
        uint64_t splineId = 0;
        events::splineTerrain::HeightLayerSnapshot before;
        events::splineTerrain::HeightLayerSnapshot after;

        static void applyEndpoint(const events::splineTerrain::HeightLayerSnapshot& endpoint,
                                  uint64_t layerId);

    public:
        HeightLayerRecordUndoCommand(std::string desc,
                                     events::splineTerrain::HeightLayerSnapshot beforeState,
                                     events::splineTerrain::HeightLayerSnapshot afterState);

        // False when the operation touched no layer at all -- a paint- or mesh-only spline apply
        // reaches the push site with both endpoints absent, and an entry that does nothing on undo
        // is worse than no entry: it eats a Ctrl+Z.
        [[nodiscard]] bool hasChange() const { return before.present || after.present; }

        void execute() override; // redo
        void undo() override;

        std::string getDescription() const override { return description; }
        size_t getMemoryFootprint() const override;
    };

    // Hide / show. Parametric and self-inverse: the payload is one bool, and the terrain is
    // recomputed from the base rather than replayed from bytes.
    class HeightLayerVisibilityUndoCommand : public IUndoableCommand
    {
    private:
        std::string description;
        uint64_t splineId = 0;
        bool visibleBefore = false;
        bool visibleAfter = false;

    public:
        HeightLayerVisibilityUndoCommand(std::string desc, uint64_t layerId, bool before,
                                         bool after);

        void execute() override;
        void undo() override;

        std::string getDescription() const override { return description; }
        size_t getMemoryFootprint() const override { return description.capacity(); }
    };

    // Reorder. Stores the two stack INDICES, not the stack.
    //
    // That is exact rather than approximate: moveLayer is a std::rotate of one element, which is a
    // bijection on the stack, so moving the same layer back to the index it came from restores the
    // precise permutation -- including the positions of every layer the move slid past.
    class HeightLayerOrderUndoCommand : public IUndoableCommand
    {
    private:
        std::string description;
        uint64_t splineId = 0;
        uint32_t indexBefore = 0;
        uint32_t indexAfter = 0;

    public:
        HeightLayerOrderUndoCommand(std::string desc, uint64_t layerId, uint32_t before,
                                    uint32_t after);

        void execute() override;
        void undo() override;

        std::string getDescription() const override { return description; }
        size_t getMemoryFootprint() const override { return description.capacity(); }
    };

    // Rename. Metadata only: neither direction invalidates a tile or recomposes anything.
    class HeightLayerNameUndoCommand : public IUndoableCommand
    {
    private:
        std::string description;
        uint64_t splineId = 0;
        std::string nameBefore;
        std::string nameAfter;

    public:
        HeightLayerNameUndoCommand(std::string desc, uint64_t layerId, std::string before,
                                   std::string after);

        void execute() override;
        void undo() override;

        std::string getDescription() const override { return description; }
        size_t getMemoryFootprint() const override
        {
            return description.capacity() + nameBefore.capacity() + nameAfter.capacity();
        }
    };
}

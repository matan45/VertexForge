#pragma once
#include "UndoTypes.hpp"
#include <vector>
#include <cstdint>
#include <string>

namespace services
{
    // Undo command for a single cave brush stroke. Captures the full before/after
    // SDF grid and hole mask of every tile the stroke touched, so undo/redo is robust
    // (a stroke can create, deepen, or remove a cave, and also punches surface holes).
    // Restore is applied through the RestoreCaveStateCommand event so the command never
    // holds a pointer to the terrain service.
    class CaveStrokeUndoCommand : public IUndoableCommand
    {
    private:
        struct TileSnapshot
        {
            int32_t tileX = 0;
            int32_t tileZ = 0;
            std::vector<float> sdfBefore;
            std::vector<float> sdfAfter;
            std::vector<uint8_t> holeBefore;
            std::vector<uint8_t> holeAfter;
        };

        uint64_t entityId;
        std::vector<TileSnapshot> tiles;
        std::string description;

        void applyAll(bool useAfter);

    public:
        CaveStrokeUndoCommand(uint64_t entity, std::string desc)
            : entityId(entity), description(std::move(desc))
        {
        }

        void addTile(int32_t tileX, int32_t tileZ,
                     std::vector<float> sdfBefore, std::vector<float> sdfAfter,
                     std::vector<uint8_t> holeBefore, std::vector<uint8_t> holeAfter);

        bool hasChanges() const;

        void execute() override; // re-apply (redo)
        void undo() override;    // restore previous state
        std::string getDescription() const override { return description; }
    };
}

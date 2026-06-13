#include "CaveUndoCommands.hpp"
#include "../events/EventDispatcher.hpp"
#include "../events/terrain/CaveBrushEvents.hpp"

namespace services
{
    void CaveStrokeUndoCommand::addTile(
        int32_t tileX, int32_t tileZ,
        std::vector<float> sdfBefore, std::vector<float> sdfAfter,
        std::vector<uint8_t> holeBefore, std::vector<uint8_t> holeAfter)
    {
        tiles.push_back({tileX, tileZ,
                         std::move(sdfBefore), std::move(sdfAfter),
                         std::move(holeBefore), std::move(holeAfter)});
    }

    bool CaveStrokeUndoCommand::hasChanges() const
    {
        for (const auto& t : tiles)
        {
            if (t.sdfBefore != t.sdfAfter || t.holeBefore != t.holeAfter)
                return true;
        }
        return false;
    }

    void CaveStrokeUndoCommand::applyAll(bool useAfter)
    {
        events::caveBrush::RestoreCaveStateCommand cmd;
        cmd.entityId = entityId;
        cmd.tiles.reserve(tiles.size());
        for (const auto& t : tiles)
        {
            events::caveBrush::CaveTileState state;
            state.tileX = t.tileX;
            state.tileZ = t.tileZ;
            state.sdf = useAfter ? t.sdfAfter : t.sdfBefore;
            state.holeMask = useAfter ? t.holeAfter : t.holeBefore;
            cmd.tiles.push_back(std::move(state));
        }
        events::EventDispatcher::instance().execute(cmd);
    }

    void CaveStrokeUndoCommand::execute()
    {
        applyAll(true);
    }

    void CaveStrokeUndoCommand::undo()
    {
        applyAll(false);
    }
}

#include "SplineApplyUndoCommands.hpp"
#include "../events/EventDispatcher.hpp"

namespace
{
    void restoreHeights(const services::SplineHeightSnapshot& heights)
    {
        if (heights.empty())
            return;

        events::splineTerrain::RestoreSplineHeightsCommand cmd;
        cmd.originalHeights = heights;
        events::EventDispatcher::instance().execute(cmd);
    }

    void restoreWeights(const events::splineTerrain::SplineWeightSnapshot& weights)
    {
        if (weights.empty())
            return;

        events::splineTerrain::RestoreSplineWeightsCommand cmd;
        cmd.originalWeights = weights;
        events::EventDispatcher::instance().execute(cmd);
    }

    [[nodiscard]] size_t footprintOf(const services::SplineHeightSnapshot& heights)
    {
        size_t total = 0;
        for (const auto& [coord, plane] : heights)
            total += plane.capacity() * sizeof(float);
        return total;
    }

    [[nodiscard]] size_t footprintOf(const events::splineTerrain::SplineWeightSnapshot& weights)
    {
        size_t total = 0;
        for (const auto& [coord, map] : weights)
        {
            // layerIndices is a std::array — inline storage, no heap to account for.
            total += map.layerWeights.capacity() * sizeof(std::vector<float>);
            for (const auto& channel : map.layerWeights)
                total += channel.capacity() * sizeof(float);
        }
        return total;
    }
}

namespace services
{
    SplineApplyUndoCommand::SplineApplyUndoCommand(
        std::string desc,
        SplineHeightSnapshot beforeHeights, SplineHeightSnapshot afterHeights,
        events::splineTerrain::SplineWeightSnapshot beforeWeights,
        events::splineTerrain::SplineWeightSnapshot afterWeights)
        : description(std::move(desc)),
          heightsBefore(std::move(beforeHeights)),
          heightsAfter(std::move(afterHeights)),
          weightsBefore(std::move(beforeWeights)),
          weightsAfter(std::move(afterWeights))
    {
    }

    void SplineApplyUndoCommand::execute()
    {
        restoreHeights(heightsAfter);
        restoreWeights(weightsAfter);
    }

    void SplineApplyUndoCommand::undo()
    {
        restoreHeights(heightsBefore);
        restoreWeights(weightsBefore);
    }

    size_t SplineApplyUndoCommand::getMemoryFootprint() const
    {
        // Reported honestly, both endpoints included: PushUndoableCommand wraps everything in
        // SharedUndoCommand, which forwards this, and the undo service's byte budget is the only
        // thing stopping a few long roads from pinning tens of megabytes of tile snapshots.
        return footprintOf(heightsBefore) + footprintOf(heightsAfter)
             + footprintOf(weightsBefore) + footprintOf(weightsAfter)
             + description.capacity();
    }
}

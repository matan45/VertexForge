#include "SplineApplyUndoCommands.hpp"
#include "../events/EventDispatcher.hpp"

namespace
{
    void setHeightLayerVisible(uint64_t splineId, bool visible)
    {
        events::splineTerrain::SetSplineHeightLayerVisibleCommand cmd;
        cmd.splineId = splineId;
        cmd.visible = visible;
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
        std::string desc, uint64_t splineId, bool hasHeightLayer,
        events::splineTerrain::SplineWeightSnapshot beforeWeights,
        events::splineTerrain::SplineWeightSnapshot afterWeights)
        : description(std::move(desc)),
          splineId(splineId),
          hasHeightLayer(hasHeightLayer),
          weightsBefore(std::move(beforeWeights)),
          weightsAfter(std::move(afterWeights))
    {
    }

    void SplineApplyUndoCommand::execute()
    {
        if (hasHeightLayer)
            setHeightLayerVisible(splineId, true);
        restoreWeights(weightsAfter);
    }

    void SplineApplyUndoCommand::undo()
    {
        if (hasHeightLayer)
            setHeightLayerVisible(splineId, false);
        restoreWeights(weightsBefore);
    }

    size_t SplineApplyUndoCommand::getMemoryFootprint() const
    {
        // Reported honestly, both weight endpoints included: PushUndoableCommand wraps everything
        // in SharedUndoCommand, which forwards this, and the undo service's byte budget is the
        // only thing stopping a few long painted roads from pinning tens of megabytes.
        //
        // The height half now costs a splineId and a bool. That is the point: before VK-1645 a
        // single road across a 129x129 tile row carried two full float planes per tile.
        return footprintOf(weightsBefore) + footprintOf(weightsAfter) + description.capacity();
    }
}

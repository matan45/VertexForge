#include "SplineApplyUndoCommands.hpp"
#include "../events/EventDispatcher.hpp"

#include <glm/glm.hpp>

namespace
{
    // VK-1648. Restores the layer to one endpoint of the apply, where an absent endpoint means the
    // layer did not exist then and must not exist now.
    //
    // Both of these are the NON-RECORDING primitives. Pushing from here would clear the redo stack
    // mid-undo (UndoRedoServiceImpl::pushCommand calls clearStackBytes(redoStack)), so the user
    // could undo but never redo.
    void applyHeightEndpoint(const events::splineTerrain::HeightLayerSnapshot& endpoint,
                             uint64_t splineId)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (endpoint.present)
        {
            events::splineTerrain::RestoreHeightLayerCommand cmd;
            cmd.snapshot = endpoint;
            // ICommand<bool> is registered with registerQueryHandler, so it must go through
            // query(). execute() searches only the command table and THROWS on a miss — which
            // UndoRedoServiceImpl::undo() catches, turning the bug into a silent "Ctrl+Z did
            // nothing". This is exactly what the previous visibility-flip call site got wrong.
            dispatcher.query(cmd);
            return;
        }

        events::splineTerrain::RemoveSplineHeightLayerCommand cmd;
        cmd.splineId = splineId;
        dispatcher.execute(cmd); // ICommand<> — a real command handler
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

    [[nodiscard]] size_t footprintOf(const events::splineTerrain::HeightLayerSnapshot& snapshot)
    {
        if (!snapshot.present)
            return 0;

        // size(), not bucket_count(): the bucket array is an allocation-history artefact and would
        // make the reported number look unstable to the _DEBUG drift check.
        return snapshot.name.capacity()
               + snapshot.spline.samples.capacity() * sizeof(glm::vec3)
               + snapshot.affected.size() * (sizeof(::terrain::TileCoord) + sizeof(void*));
    }
}

namespace services
{
    SplineApplyUndoCommand::SplineApplyUndoCommand(
        std::string desc, uint64_t splineId,
        events::splineTerrain::HeightLayerSnapshot beforeLayer,
        events::splineTerrain::HeightLayerSnapshot afterLayer,
        events::splineTerrain::SplineWeightSnapshot beforeWeights,
        events::splineTerrain::SplineWeightSnapshot afterWeights)
        : description(std::move(desc)),
          splineId(splineId),
          heightBefore(std::move(beforeLayer)),
          heightAfter(std::move(afterLayer)),
          weightsBefore(std::move(beforeWeights)),
          weightsAfter(std::move(afterWeights))
    {
    }

    void SplineApplyUndoCommand::execute()
    {
        // Nothing to do when the apply registered no layer at all (a paint- or mesh-only spline):
        // removing an id no layer carries is harmless but pointless, and dispatching it would make
        // the redo of a paint-only apply look like a height edit in a log.
        if (heightBefore.present || heightAfter.present)
            applyHeightEndpoint(heightAfter, splineId);
        restoreWeights(weightsAfter);
    }

    void SplineApplyUndoCommand::undo()
    {
        if (heightBefore.present || heightAfter.present)
            applyHeightEndpoint(heightBefore, splineId);
        restoreWeights(weightsBefore);
    }

    size_t SplineApplyUndoCommand::getMemoryFootprint() const
    {
        // Reported honestly, all four endpoints included: PushUndoableCommand wraps everything in
        // SharedUndoCommand, which forwards this, and the undo service's byte budget is the only
        // thing stopping a few long painted roads from pinning tens of megabytes.
        return footprintOf(weightsBefore) + footprintOf(weightsAfter) + footprintOf(heightBefore)
               + footprintOf(heightAfter) + description.capacity();
    }
}

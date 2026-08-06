#include "TerrainLayerUndoCommands.hpp"

#include "../events/EventDispatcher.hpp"

#include <glm/glm.hpp>

namespace
{
    void removeHeightLayer(uint64_t splineId)
    {
        events::splineTerrain::RemoveSplineHeightLayerCommand cmd;
        cmd.splineId = splineId;
        // ICommand<> -- the one member of this family with a real command handler, so execute().
        events::EventDispatcher::instance().execute(cmd);
    }

    void restoreHeightLayer(const events::splineTerrain::HeightLayerSnapshot& snapshot)
    {
        events::splineTerrain::RestoreHeightLayerCommand cmd;
        cmd.snapshot = snapshot;
        events::EventDispatcher::instance().query(cmd);
    }

    void setHeightLayerVisible(uint64_t splineId, bool visible)
    {
        events::splineTerrain::SetSplineHeightLayerVisibleCommand cmd;
        cmd.splineId = splineId;
        cmd.visible = visible;
        events::EventDispatcher::instance().query(cmd);
    }

    void moveHeightLayer(uint64_t splineId, uint32_t newIndex)
    {
        events::splineTerrain::MoveHeightLayerCommand cmd;
        cmd.splineId = splineId;
        cmd.newIndex = newIndex;
        events::EventDispatcher::instance().query(cmd);
    }

    void setHeightLayerName(uint64_t splineId, const std::string& name)
    {
        events::splineTerrain::SetHeightLayerNameCommand cmd;
        cmd.splineId = splineId;
        cmd.name = name;
        events::EventDispatcher::instance().query(cmd);
    }

    [[nodiscard]] size_t footprintOf(const events::splineTerrain::HeightLayerSnapshot& snapshot)
    {
        if (!snapshot.present)
            return 0;

        // size(), never bucket_count(): the bucket array is an allocation-history artefact, so two
        // sets holding the same coords can report different numbers. UndoRedoServiceImpl recomputes
        // every footprint under _DEBUG and logs on drift, so an unstable number is a false alarm
        // on every call.
        return snapshot.name.capacity()
               + snapshot.spline.samples.capacity() * sizeof(glm::vec3)
               + snapshot.affected.size()
                     * (sizeof(::terrain::TileCoord) + sizeof(void*));
    }
}

namespace services
{
    HeightLayerRecordUndoCommand::HeightLayerRecordUndoCommand(
        std::string desc, events::splineTerrain::HeightLayerSnapshot beforeState,
        events::splineTerrain::HeightLayerSnapshot afterState)
        : description(std::move(desc)),
          before(std::move(beforeState)),
          after(std::move(afterState))
    {
        // Both endpoints describe the SAME layer, so either one can name it. Taking it from
        // whichever is present is what lets an add (no `before`) and a delete (no `after`) share
        // one command.
        splineId = before.present ? before.id : after.id;
    }

    void HeightLayerRecordUndoCommand::applyEndpoint(
        const events::splineTerrain::HeightLayerSnapshot& endpoint, uint64_t layerId)
    {
        if (endpoint.present)
            restoreHeightLayer(endpoint);
        else
            removeHeightLayer(layerId);
    }

    void HeightLayerRecordUndoCommand::execute()
    {
        applyEndpoint(after, splineId);
    }

    void HeightLayerRecordUndoCommand::undo()
    {
        applyEndpoint(before, splineId);
    }

    size_t HeightLayerRecordUndoCommand::getMemoryFootprint() const
    {
        // Both endpoints counted. A first apply pays only for `after` -- roughly 32 KB for a 1 km
        // road sampled at half a metre, plus its affected coords. That is a real cost VK-1645 had
        // driven to nearly zero, but it is not the old snapshot returning: that stored two float
        // planes PER TILE, which is three orders of magnitude more across the same road.
        return footprintOf(before) + footprintOf(after) + description.capacity();
    }

    HeightLayerVisibilityUndoCommand::HeightLayerVisibilityUndoCommand(std::string desc,
                                                                       uint64_t layerId,
                                                                       bool beforeState,
                                                                       bool afterState)
        : description(std::move(desc)),
          splineId(layerId),
          visibleBefore(beforeState),
          visibleAfter(afterState)
    {
    }

    void HeightLayerVisibilityUndoCommand::execute()
    {
        setHeightLayerVisible(splineId, visibleAfter);
    }

    void HeightLayerVisibilityUndoCommand::undo()
    {
        setHeightLayerVisible(splineId, visibleBefore);
    }

    HeightLayerOrderUndoCommand::HeightLayerOrderUndoCommand(std::string desc, uint64_t layerId,
                                                             uint32_t beforeIndex,
                                                             uint32_t afterIndex)
        : description(std::move(desc)),
          splineId(layerId),
          indexBefore(beforeIndex),
          indexAfter(afterIndex)
    {
    }

    void HeightLayerOrderUndoCommand::execute()
    {
        moveHeightLayer(splineId, indexAfter);
    }

    void HeightLayerOrderUndoCommand::undo()
    {
        moveHeightLayer(splineId, indexBefore);
    }

    HeightLayerNameUndoCommand::HeightLayerNameUndoCommand(std::string desc, uint64_t layerId,
                                                           std::string beforeName,
                                                           std::string afterName)
        : description(std::move(desc)),
          splineId(layerId),
          nameBefore(std::move(beforeName)),
          nameAfter(std::move(afterName))
    {
    }

    void HeightLayerNameUndoCommand::execute()
    {
        setHeightLayerName(splineId, nameAfter);
    }

    void HeightLayerNameUndoCommand::undo()
    {
        setHeightLayerName(splineId, nameBefore);
    }
}

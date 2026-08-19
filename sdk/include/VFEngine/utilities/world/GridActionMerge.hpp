#pragma once

#include "WorldExport.hpp"
#include "SectorStreamer.hpp"
#include "WorldTypes.hpp"
#include <cstdint>
#include <vector>

namespace world
{
    // VK-1599: a streaming action tagged with the grid that produced it. The service runs one
    // SectorStreamer per grid, and the action switch that consumes them needs to know which grid's
    // manager, request map and blob cache an action refers to - a bare SectorCoord is ambiguous
    // the moment a world has two grids.
    struct GridStreamingAction
    {
        uint8_t gridIndex = kPrimaryGridIndex;
        SectorStreamingAction action;
    };

    // Round-robin interleave of the per-grid action lists into one execution order.
    //
    // Why round robin rather than concatenation: the lists are consumed in order against shared,
    // finite downstream resources - the single SectorEntityLoader spawn queue and the
    // ResourceLoadScheduler's queue depth. Concatenating would let a grid with a wide ring spend
    // the whole frame's spawn budget before a grid with two urgent activations was reached, and
    // that grid would starve for as long as the first kept producing. Interleaving bounds the
    // worst case at one action of lag per grid per round.
    //
    // Each grid's own ordering is preserved exactly: SectorStreamer has already sorted its
    // candidates by distance, priority and view bias, and reordering within a grid would discard
    // that. Grid 0 leads every round, which is the tie-break the primary grid deserves - it is the
    // one carrying terrain, navmesh and the landmarks the player is looking at.
    //
    // `outMerged` is cleared, not freed, so a caller reusing a member vector allocates nothing in
    // the steady state.
    VF_WORLD_API void mergeGridStreamingActions(
        const std::vector<std::vector<SectorStreamingAction>>& perGridActions,
        std::vector<GridStreamingAction>& outMerged);

} // namespace world

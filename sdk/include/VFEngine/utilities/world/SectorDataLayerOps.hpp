#pragma once

#include "WorldExport.hpp"
#include "WorldSector.hpp"
#include "WorldTypes.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace world
{
    class WorldSectorManager;

    // VK-1596: pure helpers behind the editor's Data Layers tab.
    //
    // They live here rather than in WorldSectorServiceImpl because that class needs a
    // SceneGraphSystem and drives EventDispatcher throughout, so it is never constructed by a test
    // (see test_hlod_cell_planner.cpp). WorldSectorManager, by contrast, is unit-constructible -
    // same split as VK-1595's effectiveStreamingConfig / normalizeStreamingConfig.

    // One row of the editor's Data Layers list: a layer name plus its footprint across the
    // summarized sectors.
    struct DataLayerSummary
    {
        std::string name;
        uint32_t sectorCount = 0;         // summarized sectors carrying this layer
        uint64_t totalBytes = 0;          // exact sum of the blobs, not an on-disk estimate
        std::vector<SectorCoord> sectors; // sorted; drives the grid highlight and tooltips
    };

    // Whether a sector may have its data layers mutated. This is a DATA-LOSS guard, not a
    // convenience check.
    //
    // An unloaded sector's entityUUIDs is empty - loadWorld creates every sector Unloaded with a
    // file path and no UUIDs, and handleSectorUnload strips them back to just the dynamic ones -
    // while saveWorld re-saves any sector that is merely `dirty`, and saveSector rebuilds file
    // content by resolving entityUUIDs against the registry. So letting a layer write dirty an
    // unloaded sector makes the next Save World overwrite a good .vfsector with an empty one.
    //
    // Loading is excluded for a second reason: finalizeSectorLoad would clear the dirty flag the
    // write had just set, and entityUUIDs is not populated until it runs.
    [[nodiscard]] inline bool canMutateDataLayers(const WorldSector& sector) noexcept
    {
        return sector.state == SectorState::Loaded;
    }

    // Everything the editor's Data Layers tab needs, from one pass over the sectors.
    //
    // `loadedSectors` is carried here rather than fetched from GetLoadedSectorCoordsQuery on the
    // side because that query also admits Loading sectors, which canMutateDataLayers refuses. Two
    // subtly different definitions of "loaded" in one panel is how you get a picker that offers a
    // sector every write then silently rejects.
    struct DataLayerInventory
    {
        std::vector<SectorCoord> loadedSectors;  // sorted; every Loaded sector, layers or not
        std::vector<DataLayerSummary> layers;    // sorted by name
    };

    // Union of layer names across every LOADED sector.
    //
    // The name sort is load-bearing, not cosmetic: SectorDataLayers is an unordered_map, so an
    // unsorted list would reshuffle itself on every poll of the 0.25s editor refresh timer and
    // move rows out from under the cursor.
    //
    // Loaded only, deliberately. A Loading sector has not merged its file layers yet
    // (finalizeSectorLoad does that), and a Prefetched sector holds raw bytes with no live layers
    // at all - listing either would show a footprint that is about to change on its own.
    VF_WORLD_API void summarizeDataLayers(const WorldSectorManager& manager,
                                          DataLayerInventory& out);

    // Merges `incoming` (just read from the .vfsector) UNDER `resident` (already in memory): a
    // resident layer always wins, because a layer written at runtime - fog of war, say - survives
    // unload on the WorldSector struct and is newer than what the file holds.
    //
    // Returns true when `resident` ends up holding a layer the file did NOT supply, or supplied
    // with different bytes - i.e. unsaved work that Save World must not skip. `incoming` is left
    // in a moved-from state.
    [[nodiscard]] VF_WORLD_API bool mergeSectorDataLayers(SectorDataLayers& resident,
                                                          SectorDataLayers& incoming);

} // namespace world

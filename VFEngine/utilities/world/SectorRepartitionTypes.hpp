#pragma once

#include <cstdint>
#include <string>

namespace world
{
    // VK-1598: the counters the repartition dry run reports and the apply logs.
    //
    // Deliberately in its own header, free of <nlohmann/json.hpp>: it crosses the DLL boundary in
    // PreviewRepartitionQuery, so WorldSectorEvents.hpp includes it - and that header is mirrored
    // into sdk/ for plugins. The json-taking plan API lives in SectorRepartitionPlanner.hpp, which
    // only the World DLL, the service ops and the tests include. Same split as VK-1596's
    // DataLayerInventory vs its ops.
    struct RepartitionSummary
    {
        // Whether the plan is applicable at all. False means a guard refused, and `refusal` says
        // which - the dialog shows it verbatim rather than re-deriving the rule.
        bool valid = false;
        std::string refusal;

        uint32_t sourceSectorCount = 0;  // .vfsector files read
        uint32_t targetSectorCount = 0;  // .vfsector files that would be written
        uint32_t entityCount = 0;        // unique top-level entities carried across
        uint32_t movedCount = 0;         // entities whose sector coord changes

        // Entities listed more than once in a source file and collapsed to one. Non-zero means the
        // world was written by the pre-VK-1598 save path, which iterated an entityUUIDs list that
        // held every UUID twice - see WorldSectorServiceImpl's setOnEntityLoaded.
        uint32_t duplicatesDropped = 0;

        // Terrain / ocean / IBL / camera payloads and entities pinned !spatiallyLoaded. They are
        // NOT re-bucketed, but they are still carried - the .vfsector is the only copy of their
        // data, so dropping them would be data loss.
        uint32_t carriedNonSpatial = 0;

        // Data-layer blobs are opaque and cannot be split, so one source blob is copied to every
        // geometrically overlapping target. `layerCopies` counts the writes, `layerNameCollisions`
        // the times two source blobs of the same name landed in one target (first source wins,
        // ordered by (z,x)).
        uint32_t layerCopies = 0;
        uint32_t layerNameCollisions = 0;

        // Entities that are inside the addressable extent under the CURRENT sector size and would
        // fall outside it under the new one. Non-zero is fatal.
        //
        // Deliberately counted on the ENTITY, not on the resulting sector: worldPositionToSectorCoord
        // clamps in float space before the int cast (VK-1588), so an out-of-range position silently
        // folds into the boundary sector and a check on the produced coord could never fire.
        // Shrinking sectorWorldSize is what reaches this - halving it doubles every coord.
        //
        // An entity that is ALREADY outside the extent is not counted: it is clamped today and
        // would be clamped afterwards, so the migration neither causes nor worsens it.
        uint32_t outOfRangeEntities = 0;

        // Sum of the source .vfsector file sizes - the honest lower bound on peak memory, since
        // the op holds every entity's JSON DOM at once. Surfaced in the dialog.
        uint64_t sourceBytes = 0;
    };

} // namespace world

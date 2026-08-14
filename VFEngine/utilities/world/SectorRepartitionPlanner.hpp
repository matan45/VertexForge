#pragma once

#include "WorldExport.hpp"
#include "SectorAssignment.hpp"
#include "SectorRepartitionTypes.hpp"
#include "WorldSector.hpp"
#include "WorldTypes.hpp"
#include <cstdint>
#include <nlohmann/json.hpp>
#include <vector>

namespace world
{
    // VK-1598: the pure half of the world re-partition tool - "given every sector's serialized
    // contents under the old SectorConfig, what does the sector set look like under a new one".
    //
    // It works on the .vfsector JSON directly and never touches the ECS. That is the whole point:
    // an entity node moved verbatim from one sector's array into another's cannot lose its UUID,
    // its components or its children, because nothing ever deserializes it. SectorAssignment's
    // EntityStreamingTraits was made a registry-free POD for exactly this caller
    // (see SectorAssignment.hpp).
    //
    // It lives here rather than in WorldSectorServiceImpl because that class needs a
    // SceneGraphSystem and drives EventDispatcher throughout, so it is never constructed by a test
    // - the same split as VK-1596's SectorDataLayerOps and VK-1594's HLODCellPlanner.

    // Reads the bucketing traits off a SERIALIZED entity node. The registry-side twin is
    // world::readEntityStreamingTraits; the two must agree, and test_sector_repartition pins that
    // by feeding this the output of SceneSerialization::serializeEntity.
    //
    // Node shape (SceneSerialization::serializeEntityImpl):
    //   { "uuid", "name", "isActive", "transform": {...}, "components": {...}, "children": [...] }
    [[nodiscard]] VF_WORLD_API EntityStreamingTraits
        readEntityStreamingTraitsFromJson(const nlohmann::json& entityJson);

    // One source .vfsector, already read off disk. `entities` is consumed - planRepartition MOVES
    // the nodes into the targets rather than copying them.
    struct RepartitionSourceSector
    {
        SectorCoord coord;
        std::vector<nlohmann::json> entities;
        SectorDataLayers dataLayers;
        uint64_t fileBytes = 0; // on-disk size, reported as the peak-memory lower bound
    };

    struct RepartitionTargetSector
    {
        SectorCoord coord;
        std::vector<nlohmann::json> entities;
        SectorDataLayers dataLayers;
    };

    struct RepartitionPlan
    {
        std::vector<RepartitionTargetSector> targets; // sorted by (z, x)
        RepartitionSummary summary;
    };

    // Every target sector under `newConfig` that the source sector's world-space footprint under
    // `oldConfig` overlaps. Used to fan a data-layer blob out: blobs are opaque byte arrays, so
    // they cannot be split and the only lossless migration is to copy each one to every overlapping
    // target.
    //
    // The footprint is half-open, [x*size, (x+1)*size), matching worldPositionToSectorCoord - so a
    // shared edge does NOT report the neighbour on the far side of it.
    //
    // Returns EMPTY when the footprint would span more than kMaxSectorFanout targets. Shrinking
    // sectorWorldSize by a large factor makes this quadratic, and an unbounded loop here is a
    // multi-gigabyte allocation, not a slow one. planRepartition refuses the whole op on the same
    // rule before it ever gets here, so the empty result can never silently drop a blob.
    inline constexpr size_t kMaxSectorFanout = 4096;

    [[nodiscard]] VF_WORLD_API std::vector<SectorCoord>
        overlappingTargetSectors(const SectorCoord& source, const SectorConfig& oldConfig,
                                 const SectorConfig& newConfig);

    // Target cells one source sector's footprint spans, as a double so an explosive ratio cannot
    // overflow the check itself. The guard both planRepartition and overlappingTargetSectors use.
    [[nodiscard]] VF_WORLD_API double sectorFanout(const SectorConfig& oldConfig,
                                                   const SectorConfig& newConfig) noexcept;

    // Whether this position resolves to a sector inside [kMinSectorCoord, kMaxSectorCoord].
    //
    // Needed because worldPositionToSectorCoord CLAMPS (VK-1588: the clamp is in float space,
    // before the int cast, because casting an out-of-range float is UB). The clamp is the right
    // behaviour at runtime - a boundary sector beats undefined - but it means the produced coord is
    // always in range, so a migration cannot detect the hazard by inspecting its own output. The
    // division is done in double so the comparison is exact at the extremes.
    [[nodiscard]] VF_WORLD_API bool isWithinAddressableExtent(const glm::vec3& position,
                                                              const SectorConfig& config) noexcept;

    // The whole re-bucket, in one pass. `sources` is left in a moved-from state.
    //
    // The dry run and the apply call this identically - the summary is a by-product of building
    // the plan, never a second traversal, so what the dialog shows is exactly what Apply writes.
    [[nodiscard]] VF_WORLD_API RepartitionPlan planRepartition(std::vector<RepartitionSourceSector>& sources,
                                                               const SectorConfig& oldConfig,
                                                               const SectorConfig& newConfig);

} // namespace world

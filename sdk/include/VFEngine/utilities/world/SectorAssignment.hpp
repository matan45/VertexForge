#pragma once

#include "WorldExport.hpp"
#include "WorldSector.hpp"
#include "WorldTypes.hpp"
#include <cstdint>
#include <glm/glm.hpp>

namespace scene
{
    class Entity;
}

namespace world
{
    // VK-1597: the entity-to-sector bucketing decision, pulled out of the two byte-identical
    // isManagedBySeparateSystem() copies that lived in anonymous namespaces in
    // WorldSectorPersistenceOps.cpp and WorldSectorServiceImpl.cpp.
    //
    // It lives here rather than in WorldSectorServiceImpl because that class needs a
    // SceneGraphSystem and drives EventDispatcher throughout, so it is never constructed by a test
    // (see test_hlod_cell_planner.cpp). Same split as VK-1596's SectorDataLayerOps and VK-1595's
    // effectiveStreamingConfig / normalizeStreamingConfig.

    // Why an entity is, or is not, bucketed into a spatial sector.
    enum class SectorAssignmentKind : uint8_t
    {
        Spatial,                 // bucketed - SectorAssignment::coord is meaningful
        ManagedBySeparateSystem, // terrain / terrain tile / ocean / IBL / camera
        NotSpatiallyLoaded,      // StreamingPolicyComponent{ spatiallyLoaded = false }
        NoTransform,             // nothing to derive a position from
    };

    // Everything the decision needs, with no registry dependency, so the decision itself is a pure
    // function. Deliberately a POD rather than taking a scene::Entity: VK-1598's repartition tool
    // works on entities read back out of .vfsector JSON with no live entity to hand, and VK-1599
    // adds a grid id here without touching a single call site.
    struct EntityStreamingTraits
    {
        bool managedBySeparateSystem = false;
        bool spatiallyLoaded = true;
        bool hasTransform = false;
        glm::vec3 position{0.0f};
    };

    struct SectorAssignment
    {
        SectorAssignmentKind kind = SectorAssignmentKind::NoTransform;
        SectorCoord coord{};

        [[nodiscard]] bool isSpatial() const noexcept { return kind == SectorAssignmentKind::Spatial; }
    };

    // The floor-to-sector-axis math WorldSectorManager has always used, lifted here verbatim so
    // the pure decision and the manager cannot drift apart. WorldSectorManager now delegates.
    //
    // VK-1588 semantics preserved exactly: the clamp happens in FLOAT space, before the int cast,
    // because casting a NaN or an out-of-int32-range float is UB - a post-cast range check would
    // be checking a value the compiler was free to invent. A zero or negative sectorWorldSize
    // makes the division inf/NaN, which collapses to sector 0 rather than dividing into garbage.
    [[nodiscard]] VF_WORLD_API SectorCoord worldPositionToSectorCoord(const glm::vec3& position,
                                                                      const SectorConfig& config) noexcept;

    // Same, for a terrain tile coordinate.
    [[nodiscard]] VF_WORLD_API SectorCoord tileOriginToSectorCoord(int32_t tileX, int32_t tileZ,
                                                                   float worldTileSize,
                                                                   const SectorConfig& config) noexcept;

    [[nodiscard]] VF_WORLD_API SectorAssignment resolveSectorAssignment(const EntityStreamingTraits& traits,
                                                                        const SectorConfig& config) noexcept;

    // Registry-side adapter: reads the traits off a live entity. Not pure - it touches the ECS
    // registry - which is exactly why it is separated from resolveSectorAssignment above.
    //
    // scene::Entity is forward-declared in this header on purpose; Entity.hpp is included only by
    // the .cpp so that including SectorAssignment.hpp does not drag entt and EntityRegistry into
    // every consumer.
    [[nodiscard]] VF_WORLD_API EntityStreamingTraits readEntityStreamingTraits(const scene::Entity& entity);

    // Convenience for the common "live entity -> where does it go" call.
    [[nodiscard]] VF_WORLD_API SectorAssignment resolveEntitySectorAssignment(const scene::Entity& entity,
                                                                              const SectorConfig& config);

    // Whether an entity may be moved into or out of this sector's membership. This is a DATA-LOSS
    // guard, not a convenience check - the same one VK-1596 needed for data layers
    // (world::canMutateDataLayers).
    //
    // An unloaded sector's entityUUIDs is empty or holds only the dynamic leftovers: loadWorld
    // creates every sector Unloaded with a file path and no UUIDs, and handleSectorUnload strips
    // them back to the dynamic ones. saveWorld re-saves any sector that is merely `dirty`, and
    // saveSector rebuilds file content by resolving entityUUIDs against the registry. So dirtying
    // a non-Loaded sector makes the next Save World overwrite a good .vfsector with an empty one.
    //
    // Loading is excluded for a second reason: entityUUIDs is not populated until
    // finalizeSectorLoad runs, and that would then clear the dirty flag this write had just set.
    [[nodiscard]] inline bool canMigrateEntity(const WorldSector& sector) noexcept
    {
        return sector.state == SectorState::Loaded;
    }

} // namespace world

#include "SectorAssignment.hpp"
#include "../components/Components.hpp"
#include "../scene/Entity.hpp"
#include <algorithm>
#include <cmath>

namespace world
{
    namespace
    {
        // VK-1588, moved verbatim out of WorldSectorManager.cpp's anonymous namespace: clamp in
        // FLOAT space, before the int32 cast. Casting a NaN or an out-of-int32-range float is UB,
        // so a post-cast range check would be checking a value the compiler was free to invent.
        // This also absorbs a zero or negative sectorWorldSize, which makes the division inf/NaN -
        // those collapse to sector 0 rather than dividing into garbage. Both bounds are < 2^24, so
        // they are exactly representable as float and the clamp is lossless.
        int32_t floorToSectorAxis(float value) noexcept
        {
            if (!std::isfinite(value))
                return 0;

            const float f = std::clamp(std::floor(value),
                                       static_cast<float>(kMinSectorCoord),
                                       static_cast<float>(kMaxSectorCoord));
            return static_cast<int32_t>(f);
        }
    }

    SectorCoord worldPositionToSectorCoord(const glm::vec3& position, const SectorConfig& config) noexcept
    {
        return SectorCoord(
            floorToSectorAxis(position.x / config.sectorWorldSize),
            floorToSectorAxis(position.z / config.sectorWorldSize)
        );
    }

    SectorCoord tileOriginToSectorCoord(int32_t tileX, int32_t tileZ, float worldTileSize,
                                        const SectorConfig& config) noexcept
    {
        const float worldX = static_cast<float>(tileX) * worldTileSize;
        const float worldZ = static_cast<float>(tileZ) * worldTileSize;
        return SectorCoord(
            floorToSectorAxis(worldX / config.sectorWorldSize),
            floorToSectorAxis(worldZ / config.sectorWorldSize)
        );
    }

    namespace
    {
        // The three refusals, shared by both overloads. Returns true when a refusal was written.
        bool resolveRefusal(const EntityStreamingTraits& traits, SectorAssignment& out) noexcept
        {
            // Order matters for diagnostics only - the three refusals are mutually exclusive in
            // practice - but the type skip-list is checked first so it keeps reporting the reason
            // it reported before VK-1597 existed.
            if (traits.managedBySeparateSystem)
            {
                out.kind = SectorAssignmentKind::ManagedBySeparateSystem;
                return true;
            }

            if (!traits.spatiallyLoaded)
            {
                out.kind = SectorAssignmentKind::NotSpatiallyLoaded;
                return true;
            }

            if (!traits.hasTransform)
            {
                out.kind = SectorAssignmentKind::NoTransform;
                return true;
            }

            return false;
        }
    }

    SectorAssignment resolveSectorAssignment(const EntityStreamingTraits& traits,
                                             std::span<const SectorConfig> gridConfigs) noexcept
    {
        SectorAssignment result;
        if (resolveRefusal(traits, result))
            return result;

        // VK-1599: an out-of-range grid resolves on the primary grid rather than refusing. A
        // .vfscene can name a grid a later edit of the .vfworld removed, and dropping the entity
        // out of the sector system entirely would be data loss on the next Save World.
        const bool gridExists = traits.gridIndex < gridConfigs.size();
        result.gridIndex = gridExists ? traits.gridIndex : kPrimaryGridIndex;

        // An empty span means "no world open"; the struct default is the same 128-unit grid a new
        // world starts from, which is the only sensible answer and never reads out of bounds.
        const SectorConfig fallback;
        const SectorConfig& config = gridConfigs.empty() ? fallback : gridConfigs[result.gridIndex];

        result.kind = SectorAssignmentKind::Spatial;
        result.coord = worldPositionToSectorCoord(traits.position, config);
        return result;
    }

    SectorAssignment resolveSectorAssignment(const EntityStreamingTraits& traits,
                                             const SectorConfig& config) noexcept
    {
        SectorAssignment result;
        if (resolveRefusal(traits, result))
            return result;

        result.kind = SectorAssignmentKind::Spatial;
        result.gridIndex = kPrimaryGridIndex;
        result.coord = worldPositionToSectorCoord(traits.position, config);
        return result;
    }

    EntityStreamingTraits readEntityStreamingTraits(const scene::Entity& entity)
    {
        EntityStreamingTraits traits;

        traits.managedBySeparateSystem =
               entity.hasComponent<components::TerrainComponent>()
            || entity.hasComponent<components::TerrainTileComponent>()
            || entity.hasComponent<components::OceanComponent>()
            || entity.hasComponent<components::IBLComponent>()
            || entity.hasComponent<components::CameraComponent>();

        // Absent means spatially loaded on the primary grid - see StreamingPolicyComponent.
        if (entity.hasComponent<components::StreamingPolicyComponent>())
        {
            const auto& policy = entity.getComponent<components::StreamingPolicyComponent>();
            traits.spatiallyLoaded = policy.spatiallyLoaded;
            traits.gridIndex = policy.gridIndex;
        }

        if (entity.hasComponent<components::TransformComponent>())
        {
            traits.hasTransform = true;
            traits.position = entity.getComponent<components::TransformComponent>().position;
        }

        return traits;
    }

    SectorAssignment resolveEntitySectorAssignment(const scene::Entity& entity, const SectorConfig& config)
    {
        return resolveSectorAssignment(readEntityStreamingTraits(entity), config);
    }

} // namespace world

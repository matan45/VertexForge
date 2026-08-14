#include "SectorRepartitionPlanner.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace world
{
    namespace
    {
        // Same ordering HLODCellPlanner uses: row-major by (z, x). Both the source scan and the
        // target list are sorted with it, because every container in play is an unordered_map -
        // without it the written file set, the collision winner and the progress bar would all
        // vary run to run.
        bool sectorCoordLess(const SectorCoord& a, const SectorCoord& b) noexcept
        {
            if (a.z != b.z)
                return a.z < b.z;
            return a.x < b.x;
        }

        // The world-space sector a point falls in, going through the exported coord math so the
        // clamp/NaN handling in floorToSectorAxis is inherited rather than re-derived.
        SectorCoord coordAt(float x, float z, const SectorConfig& config) noexcept
        {
            return worldPositionToSectorCoord(glm::vec3(x, 0.0f, z), config);
        }

        // The largest float strictly below `value`. Turns the half-open upper bound of a sector's
        // footprint into a point that is genuinely inside it, so the shared edge between two
        // sectors is attributed to exactly one of them - the same rule worldPositionToSectorCoord
        // applies to an entity sitting on the boundary.
        float justBelow(float value) noexcept
        {
            return std::nextafter(value, -std::numeric_limits<float>::infinity());
        }
    }

    EntityStreamingTraits readEntityStreamingTraitsFromJson(const nlohmann::json& entityJson)
    {
        EntityStreamingTraits traits;

        if (!entityJson.is_object())
            return traits;

        // Mirrors readEntityStreamingTraits' component list exactly (SectorAssignment.cpp:83-88).
        // The keys are the ones SceneSerializeDispatch writes for those components.
        if (auto components = entityJson.find("components");
            components != entityJson.end() && components->is_object())
        {
            traits.managedBySeparateSystem =
                   components->contains("terrain")
                || components->contains("terrainTile")
                || components->contains("ocean")
                || components->contains("ibl")
                || components->contains("camera");

            // Absent means spatially loaded - see StreamingPolicyComponent (VK-1597).
            if (auto policy = components->find("streamingPolicy");
                policy != components->end() && policy->is_object())
            {
                traits.spatiallyLoaded = policy->value("spatiallyLoaded", true);
            }
        }

        // serializeEntityImpl writes "transform" only when the entity has a TransformComponent,
        // so its presence IS hasTransform.
        if (auto transform = entityJson.find("transform");
            transform != entityJson.end() && transform->is_object())
        {
            traits.hasTransform = true;

            // A malformed element leaves the axis at its default rather than throwing: this is a
            // migration tool, and refusing to move an entity is worse than reading a 0 for a
            // component that deserializeTransform would also have left at its default.
            if (auto position = transform->find("position");
                position != transform->end() && position->is_array() && position->size() >= 3)
            {
                for (int axis = 0; axis < 3; ++axis)
                {
                    if ((*position)[axis].is_number())
                        traits.position[axis] = (*position)[axis].get<float>();
                }
            }
        }

        return traits;
    }

    double sectorFanout(const SectorConfig& oldConfig, const SectorConfig& newConfig) noexcept
    {
        if (!std::isfinite(oldConfig.sectorWorldSize) || oldConfig.sectorWorldSize <= 0.0f ||
            !std::isfinite(newConfig.sectorWorldSize) || newConfig.sectorWorldSize <= 0.0f)
            return 1.0;

        // +1 per axis: a footprint that is not grid-aligned straddles one extra cell on each side.
        const double ratio = static_cast<double>(oldConfig.sectorWorldSize) /
                             static_cast<double>(newConfig.sectorWorldSize);
        const double perAxis = ratio + 1.0;
        return perAxis * perAxis;
    }

    bool isWithinAddressableExtent(const glm::vec3& position, const SectorConfig& config) noexcept
    {
        if (!std::isfinite(config.sectorWorldSize) || config.sectorWorldSize <= 0.0f)
            return false;

        const auto axisOk = [&](float value) noexcept
        {
            if (!std::isfinite(value))
                return false;

            const double coord = std::floor(static_cast<double>(value) /
                                            static_cast<double>(config.sectorWorldSize));
            return coord >= static_cast<double>(kMinSectorCoord)
                && coord <= static_cast<double>(kMaxSectorCoord);
        };

        return axisOk(position.x) && axisOk(position.z);
    }

    std::vector<SectorCoord> overlappingTargetSectors(const SectorCoord& source,
                                                      const SectorConfig& oldConfig,
                                                      const SectorConfig& newConfig)
    {
        std::vector<SectorCoord> result;

        // A non-positive or non-finite old size has no footprint to speak of; worldPositionToSector
        // Coord collapses everything to sector 0 in that case, so mirror it rather than looping
        // over garbage bounds.
        if (!std::isfinite(oldConfig.sectorWorldSize) || oldConfig.sectorWorldSize <= 0.0f)
        {
            result.push_back(coordAt(0.0f, 0.0f, newConfig));
            return result;
        }

        if (sectorFanout(oldConfig, newConfig) > static_cast<double>(kMaxSectorFanout))
            return result; // caller must have refused already - see the header

        const float size = oldConfig.sectorWorldSize;
        // The +1 is done in float, not on the int32 coord: source.x is only bounded by the caller,
        // and int32 overflow on the max corner would be UB.
        const float minX = static_cast<float>(source.x) * size;
        const float minZ = static_cast<float>(source.z) * size;
        const float maxX = (static_cast<float>(source.x) + 1.0f) * size;
        const float maxZ = (static_cast<float>(source.z) + 1.0f) * size;

        const SectorCoord lo = coordAt(minX, minZ, newConfig);
        const SectorCoord hi = coordAt(justBelow(maxX), justBelow(maxZ), newConfig);

        // lo can exceed hi only when the clamp in floorToSectorAxis folded both onto the same
        // boundary coord from opposite directions; the loop bounds below already handle that by
        // producing a single cell.
        const int32_t xEnd = std::max(lo.x, hi.x);
        const int32_t zEnd = std::max(lo.z, hi.z);

        result.reserve(static_cast<size_t>(xEnd - lo.x + 1) * static_cast<size_t>(zEnd - lo.z + 1));
        for (int32_t z = lo.z; z <= zEnd; ++z)
        {
            for (int32_t x = lo.x; x <= xEnd; ++x)
                result.emplace_back(x, z);
        }

        return result;
    }

    RepartitionPlan planRepartition(std::vector<RepartitionSourceSector>& sources,
                                    const SectorConfig& oldConfig,
                                    const SectorConfig& newConfig)
    {
        RepartitionPlan plan;
        plan.summary.sourceSectorCount = static_cast<uint32_t>(sources.size());

        // Visit the sources in a stable order. Everything downstream that can be decided two ways
        // - which duplicate survives, which data-layer blob wins a name collision - resolves to
        // "first in (z, x) order", so the whole op is reproducible.
        std::vector<size_t> order(sources.size());
        for (size_t i = 0; i < sources.size(); ++i)
            order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b)
                  { return sectorCoordLess(sources[a].coord, sources[b].coord); });

        std::unordered_map<SectorCoord, RepartitionTargetSector, SectorCoordHash> byCoord;
        std::unordered_set<uint64_t> seenUUIDs;

        auto targetFor = [&](const SectorCoord& coord) -> RepartitionTargetSector&
        {
            auto& target = byCoord[coord];
            target.coord = coord;
            return target;
        };

        // ---- entities -------------------------------------------------------------------
        for (size_t index : order)
        {
            auto& source = sources[index];
            plan.summary.sourceBytes += source.fileBytes;

            // The sector's centre, for a payload with no transform of its own: it has no position
            // to bucket by, so it stays wherever its old sector's middle now lands.
            const float centreX = (static_cast<float>(source.coord.x) + 0.5f) * oldConfig.sectorWorldSize;
            const float centreZ = (static_cast<float>(source.coord.z) + 0.5f) * oldConfig.sectorWorldSize;

            for (auto& entityJson : source.entities)
            {
                uint64_t uuid = 0;
                bool hasUUID = false;
                if (auto it = entityJson.find("uuid");
                    it != entityJson.end() && it->is_number_unsigned())
                {
                    uuid = it->get<uint64_t>();
                    hasUUID = true;
                }

                // A node listed twice is the fingerprint of the pre-VK-1598 save path, whose
                // entityUUIDs list held every UUID twice. Collapse it here so the repartitioned
                // world comes out with the entity count the user actually has. A node with no
                // usable uuid cannot be deduped, so it is carried as-is rather than dropped.
                if (hasUUID && !seenUUIDs.insert(uuid).second)
                {
                    ++plan.summary.duplicatesDropped;
                    continue;
                }

                const EntityStreamingTraits traits = readEntityStreamingTraitsFromJson(entityJson);
                const SectorAssignment assignment = resolveSectorAssignment(traits, newConfig);

                // Checked on the position, before the coord math clamps it - see
                // isWithinAddressableExtent. Only a NEWLY out-of-range entity counts: one that is
                // already outside the extent is clamped today and would be clamped afterwards.
                if (traits.hasTransform &&
                    isWithinAddressableExtent(traits.position, oldConfig) &&
                    !isWithinAddressableExtent(traits.position, newConfig))
                {
                    ++plan.summary.outOfRangeEntities;
                }

                SectorCoord targetCoord;
                if (assignment.isSpatial())
                {
                    targetCoord = assignment.coord;
                }
                else
                {
                    // "Do not re-bucket" is not "discard". A terrain/ocean/IBL/camera payload, or
                    // an entity pinned !spatiallyLoaded, still lives ONLY in this .vfsector - the
                    // scene copy that would normally own it does not exist until the user saves
                    // the scene (see WorldSectorServiceImpl's setOnEntityLoaded). Carry it.
                    ++plan.summary.carriedNonSpatial;
                    targetCoord = traits.hasTransform
                                      ? worldPositionToSectorCoord(traits.position, newConfig)
                                      : coordAt(centreX, centreZ, newConfig);
                }

                if (targetCoord != source.coord)
                    ++plan.summary.movedCount;

                ++plan.summary.entityCount;
                targetFor(targetCoord).entities.push_back(std::move(entityJson));
            }

            source.entities.clear();
        }

        // ---- data layers ----------------------------------------------------------------
        // One global check, not per source: every sector has the same footprint, so the fan-out is
        // a property of the two configs alone.
        const bool fanoutExploded =
            sectorFanout(oldConfig, newConfig) > static_cast<double>(kMaxSectorFanout);

        for (size_t index : order)
        {
            auto& source = sources[index];
            if (source.dataLayers.empty())
                continue;

            if (fanoutExploded)
            {
                // Refuse rather than copy a blob into a subset of the sectors that need it - a
                // fog-of-war grid with holes in it is worse than no migration at all.
                plan.summary.refusal =
                    "This sector size would fan each data-layer blob out across more than " +
                    std::to_string(kMaxSectorFanout) +
                    " sectors. Repartition in smaller steps, or clear the data layers first.";
                break;
            }

            const std::vector<SectorCoord> overlaps =
                overlappingTargetSectors(source.coord, oldConfig, newConfig);

            // Name order matters for the same reason the sector order does: SectorDataLayers is an
            // unordered_map, and the collision counter must not depend on bucket iteration.
            std::vector<const std::string*> layerNames;
            layerNames.reserve(source.dataLayers.size());
            for (const auto& entry : source.dataLayers)
                layerNames.push_back(&entry.first);
            std::sort(layerNames.begin(), layerNames.end(),
                      [](const std::string* a, const std::string* b) { return *a < *b; });

            for (const std::string* name : layerNames)
            {
                const auto& bytes = source.dataLayers.at(*name);
                for (const SectorCoord& coord : overlaps)
                {
                    auto& target = targetFor(coord);
                    // Blobs are opaque, so two sources contributing the same layer name to one
                    // target cannot be merged. First source in (z, x) order wins; the loser is
                    // counted and surfaced so the Data Layers tab (VK-1596) can sort it out.
                    if (!target.dataLayers.try_emplace(*name, bytes).second)
                    {
                        ++plan.summary.layerNameCollisions;
                        continue;
                    }
                    ++plan.summary.layerCopies;
                }
            }

            source.dataLayers.clear();
        }

        // ---- finalize --------------------------------------------------------------------
        plan.targets.reserve(byCoord.size());
        for (auto& [coord, target] : byCoord)
            plan.targets.push_back(std::move(target));

        std::sort(plan.targets.begin(), plan.targets.end(),
                  [](const RepartitionTargetSector& a, const RepartitionTargetSector& b)
                  { return sectorCoordLess(a.coord, b.coord); });

        plan.summary.targetSectorCount = static_cast<uint32_t>(plan.targets.size());

        // sectorCoordToId packs 16 bits per axis, so an entity folded onto the boundary sector
        // would share that sector's GPU streaming slot with whatever legitimately lives there.
        // Fatal rather than a warning - the world would be quietly wrong, not merely large.
        // Reported first because it is the one the user can act on most directly.
        if (plan.summary.outOfRangeEntities > 0)
        {
            plan.summary.refusal =
                std::to_string(plan.summary.outOfRangeEntities) +
                " entity(ies) would fall outside the addressable sector range [" +
                std::to_string(kMinSectorCoord) + ", " + std::to_string(kMaxSectorCoord) +
                "] and be clamped onto a boundary sector. Use a larger sector size.";
        }

        plan.summary.valid = plan.summary.refusal.empty();
        return plan;
    }

} // namespace world

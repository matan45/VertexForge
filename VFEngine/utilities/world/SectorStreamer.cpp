#include "SectorStreamer.hpp"
#include "../streaming/FrameBudget.hpp"
#include <algorithm>
#include <cmath>

namespace world
{
    SectorStreamer::SectorStreamer(const SectorStreamingConfig& config)
        : config(config)
    {
        // VK-1591: the ctor used to skip normalization entirely - only setConfig clamped, so a
        // directly-constructed streamer could run with unloadRadius <= loadRadius and oscillate.
        // Both paths now share normalizeConfig().
        normalizeConfig();

        // Pre-reserve to avoid per-frame growth
        activateCandidates.reserve(64);
        prefetchCandidates.reserve(64);
        unloadCandidates.reserve(32);
        ringTargets.reserve(128);
    }

    void SectorStreamer::setConfig(const SectorStreamingConfig& config)
    {
        this->config = config;
        normalizeConfig();
    }

    void SectorStreamer::normalizeConfig()
    {
        // Resolve the 0 sentinel / a sub-loadRadius value into a concrete ring so getConfig()
        // reports what the streamer will actually do (the editor re-reads it after
        // SetStreamingConfigCommand to show validated values).
        if (config.prefetchRadius < config.loadRadius)
            config.prefetchRadius = config.loadRadius;

        // Hysteresis must sit outside the OUTERMOST residency ring, not just the activate ring.
        // With prefetchRadius == loadRadius this reduces exactly to the old rule.
        if (config.unloadRadius <= config.prefetchRadius)
            config.unloadRadius = config.prefetchRadius + 1.0f;
    }

    void SectorStreamer::setEnabled(bool value)
    {
        enabled = value;
        if (value)
            needsSeed = true;
    }

    void SectorStreamer::seedTrackedSectors(const WorldSectorManager& manager)
    {
        trackedSectors.clear();
        manager.forEachSector([&](const WorldSector& sector)
        {
            if (isSectorTracked(sector.state))
                trackedSectors.insert(sector.coord);
        });
        needsSeed = false;
    }

    void SectorStreamer::update(
        const std::vector<StreamingSource>& sources,
        const WorldSectorManager& manager,
        std::vector<SectorStreamingAction>& outActions)
    {
        outActions.clear();

        if (!enabled || sources.empty())
            return;

        if (needsSeed)
            seedTrackedSectors(manager);

        const float sectorSize = manager.getConfig().sectorWorldSize;
        const float prefetchRadius = effectivePrefetchRadius(config);

        activateCandidates.clear();
        prefetchCandidates.clear();
        unloadCandidates.clear();
        ringTargets.clear();

        // Pass 1: resolve each coord's target as the MAX request over ALL sources.
        // No source "claims" a coord any more, so sources need no priority ordering here: target
        // resolution is priority-INDEPENDENT. Priority only decides who wins the frame's budget,
        // never what a sector is allowed to become.
        for (const auto& source : sources)
        {
            if (source.targetState == SectorTargetState::Unloaded)
                continue; // a source that wants nothing contributes nothing

            const float activateWorldRadius = config.loadRadius * sectorSize * source.radiusMultiplier;
            const float prefetchWorldRadius = prefetchRadius * sectorSize * source.radiusMultiplier;
            const float activateRadiusSq = activateWorldRadius * activateWorldRadius;
            const float prefetchRadiusSq = prefetchWorldRadius * prefetchWorldRadius;

            // The scan box covers the OUTER ring. When prefetchRadius == loadRadius these are the
            // identical float expressions the two-ring code used.
            const int minX = static_cast<int>(std::floor((source.position.x - prefetchWorldRadius) / sectorSize));
            const int maxX = static_cast<int>(std::floor((source.position.x + prefetchWorldRadius) / sectorSize));
            const int minZ = static_cast<int>(std::floor((source.position.z - prefetchWorldRadius) / sectorSize));
            const int maxZ = static_cast<int>(std::floor((source.position.z + prefetchWorldRadius) / sectorSize));

            for (int x = minX; x <= maxX; ++x)
            {
                for (int z = minZ; z <= maxZ; ++z)
                {
                    SectorCoord coord{x, z};

                    const WorldSector* sector = manager.getSector(coord);
                    if (!sector) continue;

                    if (isSectorTracked(sector->state))
                        trackedSectors.insert(coord);

                    const float distSq = sectorDistanceSq(coord, source.position, sectorSize);

                    SectorTargetState want;
                    if (distSq <= activateRadiusSq)
                        want = SectorTargetState::Activated;
                    else if (distSq <= prefetchRadiusSq)
                        want = SectorTargetState::Prefetched;
                    else
                        continue;

                    // A source can only ever ask for less than the ring it reaches with
                    if (want > source.targetState)
                        want = source.targetState;
                    if (want == SectorTargetState::Unloaded)
                        continue;

                    // Higher-priority sources win the per-frame budget: their candidates sort as
                    // if proportionally closer
                    const float sortKey = distSq / (1.0f + static_cast<float>(source.priority));

                    RingTarget& target = ringTargets[coord];
                    if (want > target.target)
                        target.target = want;
                    target.distSq = std::min(target.distSq, distSq);
                    if (want == SectorTargetState::Activated)
                        target.activateKey = std::min(target.activateKey, sortKey);
                    target.prefetchKey = std::min(target.prefetchKey, sortKey);
                }
            }
        }

        // Pass 2: resolved targets -> candidates
        for (const auto& [coord, target] : ringTargets)
        {
            const WorldSector* sector = manager.getSector(coord);
            if (!sector || sector->filePath.empty())
                continue;

            if (target.target == SectorTargetState::Activated)
            {
                // Unloaded    -> full load (today's path)
                // Prefetched  -> activate from the cached blob, zero file IO
                // Prefetching -> promote the in-flight read, zero extra file IO
                if (sector->state == SectorState::Unloaded ||
                    sector->state == SectorState::Prefetched ||
                    sector->state == SectorState::Prefetching)
                {
                    activateCandidates.push_back({coord, target.distSq, target.activateKey});
                }
            }
            else if (sector->state == SectorState::Unloaded)
            {
                prefetchCandidates.push_back({coord, target.distSq, target.prefetchKey});
            }
            // VK-1591 v1: NO Activated -> Prefetched demotion. A Loaded sector that drifts into
            // the prefetch band is left alone and leaves only via the unload pass at unloadRadius.
            // There is no hysteresis between the activate and prefetch rings, so demoting would
            // thrash spawn/despawn at the loadRadius boundary, and it would need a "despawn but
            // keep the blob" path through physics/animation/VFX/audio snapshot capture, GPU slots,
            // light streaming and the reference resolver. Pinned by a test.
        }

        // Pass 3: unload candidates - only unload if outside ALL sources' unload radii
        auto it = trackedSectors.begin();
        while (it != trackedSectors.end())
        {
            const WorldSector* sector = manager.getSector(*it);
            if (!sector || sector->state == SectorState::Unloaded)
            {
                it = trackedSectors.erase(it);
                continue;
            }

            // A Prefetched sector holds no entities and can never be dirty, so it needs no dirty
            // guard. Loading/Prefetching are in flight and are never evicted here.
            const bool evictable =
                (sector->state == SectorState::Loaded && !sector->dirty) ||
                sector->state == SectorState::Prefetched;

            if (evictable)
            {
                bool outsideAllSources = true;
                float maxDistSq = 0.0f;

                for (const auto& source : sources)
                {
                    // A source that wants nothing keeps nothing resident
                    if (source.targetState == SectorTargetState::Unloaded)
                        continue;

                    float unloadWorldRadius = config.unloadRadius * sectorSize * source.radiusMultiplier;
                    float unloadRadiusSq = unloadWorldRadius * unloadWorldRadius;
                    float distSq = sectorDistanceSq(*it, source.position, sectorSize);

                    if (distSq <= unloadRadiusSq)
                    {
                        outsideAllSources = false;
                        break;
                    }
                    maxDistSq = std::max(maxDistSq, distSq);
                }

                if (outsideAllSources)
                {
                    unloadCandidates.push_back({*it, maxDistSq, maxDistSq});
                }
            }
            ++it;
        }

        // Sort: load nearest (priority-scaled) first, unload farthest first.
        // ringTargets is a hash map, so its iteration order must never reach the output - the
        // coord tiebreak makes the streamer deterministic where std::sort previously left ties
        // unspecified.
        auto nearestFirst = [](const Candidate& a, const Candidate& b)
        {
            if (a.sortKey != b.sortKey) return a.sortKey < b.sortKey;
            if (a.coord.x != b.coord.x) return a.coord.x < b.coord.x;
            return a.coord.z < b.coord.z;
        };
        auto farthestFirst = [](const Candidate& a, const Candidate& b)
        {
            if (a.distSq != b.distSq) return a.distSq > b.distSq;
            if (a.coord.x != b.coord.x) return a.coord.x < b.coord.x;
            return a.coord.z < b.coord.z;
        };

        std::sort(activateCandidates.begin(), activateCandidates.end(), nearestFirst);
        std::sort(prefetchCandidates.begin(), prefetchCandidates.end(), nearestFirst);
        std::sort(unloadCandidates.begin(), unloadCandidates.end(), farthestFirst);

        // Apply per-frame budgets. Prefetches draw on their own counter so a wide prefetch ring
        // can never eat the activation budget.
        streaming::FrameBudget activateBudget(config.maxLoadsPerFrame);
        streaming::FrameBudget prefetchBudget(config.maxPrefetchesPerFrame);
        streaming::FrameBudget unloadBudget(config.maxUnloadsPerFrame);

        outActions.reserve(
            std::min(static_cast<int>(unloadCandidates.size()), config.maxUnloadsPerFrame) +
            std::min(static_cast<int>(activateCandidates.size()), config.maxLoadsPerFrame) +
            std::min(static_cast<int>(prefetchCandidates.size()), config.maxPrefetchesPerFrame));

        for (const auto& candidate : unloadCandidates)
        {
            if (!unloadBudget.tryConsume())
                break;
            trackedSectors.erase(candidate.coord);
            outActions.push_back({candidate.coord, SectorTargetState::Unloaded});
        }

        for (const auto& candidate : activateCandidates)
        {
            if (!activateBudget.tryConsume())
                break;
            trackedSectors.insert(candidate.coord);
            outActions.push_back({candidate.coord, SectorTargetState::Activated});
        }

        for (const auto& candidate : prefetchCandidates)
        {
            if (!prefetchBudget.tryConsume())
                break;
            trackedSectors.insert(candidate.coord);
            outActions.push_back({candidate.coord, SectorTargetState::Prefetched});
        }
    }

    float SectorStreamer::sectorDistanceSq(const SectorCoord& coord, const glm::vec3& cameraPos,
                                            float sectorWorldSize) const
    {
        float sectorCenterX = (static_cast<float>(coord.x) + 0.5f) * sectorWorldSize;
        float sectorCenterZ = (static_cast<float>(coord.z) + 0.5f) * sectorWorldSize;

        float dx = sectorCenterX - cameraPos.x;
        float dz = sectorCenterZ - cameraPos.z;

        return dx * dx + dz * dz;
    }

} // namespace world

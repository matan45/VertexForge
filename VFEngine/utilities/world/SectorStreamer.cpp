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
        sourceEvals.reserve(8);
        ringTargets.reserve(128);
    }

    void SectorStreamer::setConfig(const SectorStreamingConfig& config)
    {
        this->config = config;
        normalizeConfig();
    }

    void SectorStreamer::normalizeConfig()
    {
        // VK-1595: the radius clamping moved to the free normalizeStreamingConfig() so the service
        // can apply the same rules to a config it is not feeding to the streamer. getConfig() still
        // reports what the streamer will actually do (the editor re-reads it after
        // SetStreamingConfigCommand to show validated values).
        normalizeStreamingConfig(config);

        // VK-1593: turning the burst off mid-window must close the window, not let it drain at a
        // budget the config no longer describes. This is streamer STATE, so it stays here.
        if (config.burstFrames <= 0)
            burstFramesRemaining = 0;
    }

    void SectorStreamer::setEnabled(bool value)
    {
        enabled = value;
        if (value)
            needsSeed = true;
        // VK-1593: motion state is deliberately NOT reset here - see resetMotionTracking(). This
        // is a mid-frame reseed hook, not a lifecycle event.
    }

    void SectorStreamer::resetMotionTracking()
    {
        lastSourcePositions.clear();
        sourcePositionScratch.clear();
        burstFramesRemaining = 0;
        burstActive = false;
    }

    void SectorStreamer::buildSourceEvals(const std::vector<StreamingSource>& sources,
                                          float sectorWorldSize)
    {
        sourceEvals.clear();

        // Motion tracking exists only to serve the lookahead and the burst window. With both off
        // it is pure overhead, and skipping it keeps every pre-VK-1593 caller - which had no
        // reason to give its sources distinct ids - out of the id-keyed map entirely.
        const bool trackMotion = config.burstFrames > 0 || config.lookaheadSeconds > 0.0f;
        const float teleportDistance = effectiveTeleportThreshold(config) * sectorWorldSize;
        const float teleportDistanceSq = teleportDistance * teleportDistance;
        const float maxLookaheadOffset = effectivePrefetchRadius(config) * sectorWorldSize;
        bool teleportDetected = false;

        if (trackMotion)
            sourcePositionScratch.clear();
        else
            lastSourcePositions.clear(); // stale history must not survive a config flip

        for (const auto& source : sources)
        {
            if (source.targetState == SectorTargetState::Unloaded)
                continue; // a source that wants nothing contributes nothing

            bool teleported = false;
            if (trackMotion)
            {
                if (auto it = lastSourcePositions.find(source.id); it != lastSourcePositions.end())
                {
                    // XZ only, to match sectorDistanceSq: a purely vertical camera move (an RTS
                    // zoom) changes no sector distance and is not a streaming jump.
                    const float dx = source.position.x - it->second.x;
                    const float dz = source.position.z - it->second.z;
                    teleported = (dx * dx + dz * dz) > teleportDistanceSq;
                }
                // A source seen for the first time has no delta, so it is never a teleport.
                sourcePositionScratch[source.id] = source.position;
                teleportDetected = teleportDetected || teleported;
            }

            SourceEval eval;
            eval.position = source.position;
            eval.predicted = source.position;
            eval.radiusMultiplier = source.radiusMultiplier;
            eval.priority = source.priority;
            eval.targetState = source.targetState;

            // Lookahead, suppressed on a teleport frame: there the caller's velocity is the jump
            // divided by dt, which would throw the predicted ring an entire world away. This is
            // the ONE place jump-vs-motion is decided, so callers need no guard of their own.
            if (config.lookaheadSeconds > 0.0f && !teleported &&
                std::isfinite(source.velocity.x) && std::isfinite(source.velocity.z))
            {
                float offsetX = source.velocity.x * config.lookaheadSeconds;
                float offsetZ = source.velocity.z * config.lookaheadSeconds;
                const float offsetLenSq = offsetX * offsetX + offsetZ * offsetZ;

                // Never predict past the source's own outer ring. Unclamped, one bad velocity
                // turns the scan box below into a grid the size of the world - the VK-1588
                // failure mode, where a radius misread as world units became a ~1024x1024 scan.
                // Floored at 0 so a nonsensical negative radiusMultiplier cannot flip the sign of
                // the scale below and predict BACKWARDS.
                const float maxOffset = std::max(0.0f, maxLookaheadOffset * source.radiusMultiplier);
                if (offsetLenSq > maxOffset * maxOffset && offsetLenSq > 0.0f)
                {
                    const float scale = maxOffset / std::sqrt(offsetLenSq);
                    offsetX *= scale;
                    offsetZ *= scale;
                }

                eval.predicted.x += offsetX;
                eval.predicted.z += offsetZ;
            }

            // View bias works on XZ like every other streaming distance. A top-down camera's
            // forward projects to (near) zero here and falls through to omni - which is exactly
            // what the RTS wants, and why viewBiasStrength defaults to 0 as well.
            if (config.viewBiasStrength > 0.0f)
            {
                const float viewLenSq = source.viewDir.x * source.viewDir.x +
                                        source.viewDir.z * source.viewDir.z;
                if (viewLenSq > 0.0f && std::isfinite(viewLenSq))
                {
                    const float inv = 1.0f / std::sqrt(viewLenSq);
                    eval.viewDirXZ = glm::vec2(source.viewDir.x * inv, source.viewDir.z * inv);
                }
            }

            sourceEvals.push_back(eval);
        }

        if (trackMotion)
        {
            // Swap rather than erase-missing: a source that disappeared this frame simply is not
            // in the scratch map, so it drops out with no mark-and-sweep and no reallocation.
            lastSourcePositions.swap(sourcePositionScratch);

            if (teleportDetected && config.burstFrames > 0)
                burstFramesRemaining = config.burstFrames;
        }
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
        // Reset before the early return: update() is not called at all while streaming is gated
        // off, and a stale true would leave the service on the burst entity budget forever.
        burstActive = false;

        // VK-1595: the freeze gate sits here - ahead of the enabled/sources checks and ahead of
        // every piece of state below - because "pause" must mean the streamer neither decides nor
        // FORGETS anything. trackedSectors, ringTargets, burstFramesRemaining and the VK-1593
        // lastSourcePositions map are all left exactly as the last live frame left them.
        //
        // The step token is consumed unconditionally so it can never accumulate across paused
        // frames into a multi-frame burst of decisions.
        frozen = paused && !stepRequested;
        stepRequested = false;
        if (frozen)
        {
            // The skipped frame leaves lastSourcePositions describing a frame that may be minutes
            // old. Remember that, and discard it below rather than letting the next executed frame
            // read the accumulated delta as a teleport - see the clear ahead of buildSourceEvals.
            motionHistoryStale = true;
            return;
        }

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

        // VK-1595: the first executed frame after a freeze starts from a clean motion history.
        // Without this, the delta accumulated across the whole pause reads as a teleport, which
        // discards the frame's velocity and - with the wizard's burstFrames = 30 - opens a 4x
        // budget window that then spans the NEXT 30 single-steps, because burstFramesRemaining
        // only decrements on executed frames. Stepping exists to watch one decision at a time, so
        // manufacturing a 4x budget out of the act of pausing defeats the tool.
        //
        // Clearing is safe and cheap: buildSourceEvals treats a source it has not seen before as
        // "no delta, therefore no teleport" (see its first-sight note), and the per-frame velocity
        // the caller supplies is derived outside the streamer from real frame deltas, so lookahead
        // is unaffected. A burst legitimately opened BEFORE the pause is left running.
        if (motionHistoryStale)
        {
            lastSourcePositions.clear();
            sourcePositionScratch.clear();
            motionHistoryStale = false;
        }

        // Pass 0 (VK-1593): teleport guard, motion lookahead and view direction, resolved once
        // per source. Both the ring pass and the unload pass read the result, so they can never
        // disagree about where a source effectively is.
        buildSourceEvals(sources, sectorSize);

        // Pass 1: resolve each coord's target as the MAX request over ALL sources.
        // No source "claims" a coord any more, so sources need no priority ordering here: target
        // resolution is priority-INDEPENDENT. Priority only decides who wins the frame's budget,
        // never what a sector is allowed to become.
        for (const auto& eval : sourceEvals)
        {
            const float activateWorldRadius = config.loadRadius * sectorSize * eval.radiusMultiplier;
            const float prefetchWorldRadius = prefetchRadius * sectorSize * eval.radiusMultiplier;
            const float activateRadiusSq = activateWorldRadius * activateWorldRadius;
            const float prefetchRadiusSq = prefetchWorldRadius * prefetchWorldRadius;

            // The scan box covers the OUTER ring around BOTH the current and the predicted
            // position. Without the union a sector that is only near the predicted position is
            // never visited and the lookahead can never fire. With lookahead off, predicted ==
            // position and these reduce to the identical float expressions used before VK-1593.
            const float minWorldX = std::min(eval.position.x, eval.predicted.x) - prefetchWorldRadius;
            const float maxWorldX = std::max(eval.position.x, eval.predicted.x) + prefetchWorldRadius;
            const float minWorldZ = std::min(eval.position.z, eval.predicted.z) - prefetchWorldRadius;
            const float maxWorldZ = std::max(eval.position.z, eval.predicted.z) + prefetchWorldRadius;

            const int minX = static_cast<int>(std::floor(minWorldX / sectorSize));
            const int maxX = static_cast<int>(std::floor(maxWorldX / sectorSize));
            const int minZ = static_cast<int>(std::floor(minWorldZ / sectorSize));
            const int maxZ = static_cast<int>(std::floor(maxWorldZ / sectorSize));

            for (int x = minX; x <= maxX; ++x)
            {
                for (int z = minZ; z <= maxZ; ++z)
                {
                    SectorCoord coord{x, z};

                    const WorldSector* sector = manager.getSector(coord);
                    if (!sector) continue;

                    if (isSectorTracked(sector->state))
                        trackedSectors.insert(coord);

                    // VK-1593: scored against the CLOSER of where the source is and where it is
                    // predicted to be, so a sector ahead of motion enters the ring before an
                    // equidistant one behind it.
                    const float distSq = sourceDistanceSq(eval, coord, sectorSize);

                    SectorTargetState want;
                    if (distSq <= activateRadiusSq)
                        want = SectorTargetState::Activated;
                    else if (distSq <= prefetchRadiusSq)
                        want = SectorTargetState::Prefetched;
                    else
                        continue;

                    // A source can only ever ask for less than the ring it reaches with
                    if (want > eval.targetState)
                        want = eval.targetState;
                    if (want == SectorTargetState::Unloaded)
                        continue;

                    // Higher-priority sources win the per-frame budget: their candidates sort as
                    // if proportionally closer
                    float sortKey = distSq / (1.0f + static_cast<float>(eval.priority));

                    // VK-1593: push sectors away from the look direction down the ORDER only -
                    // never out of the ring, and never into the unload pass. Making residency
                    // depend on where the camera points would evict the world behind you every
                    // time it turned. Factor runs 1 (dead ahead) .. 1 + viewBiasStrength (behind).
                    if (eval.viewDirXZ.x != 0.0f || eval.viewDirXZ.y != 0.0f)
                    {
                        const float toX = (static_cast<float>(x) + 0.5f) * sectorSize - eval.position.x;
                        const float toZ = (static_cast<float>(z) + 0.5f) * sectorSize - eval.position.z;
                        const float toLenSq = toX * toX + toZ * toZ;
                        // A sector the source stands in has no direction: it is in view, unbiased.
                        if (toLenSq > 0.0f)
                        {
                            const float inv = 1.0f / std::sqrt(toLenSq);
                            const float facing = (toX * inv) * eval.viewDirXZ.x +
                                                 (toZ * inv) * eval.viewDirXZ.y;
                            sortKey *= 1.0f + config.viewBiasStrength * (1.0f - facing) * 0.5f;
                        }
                    }

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

                // sourceEvals already excludes the sources that want nothing, so a source that
                // wants nothing still keeps nothing resident.
                for (const auto& eval : sourceEvals)
                {
                    float unloadWorldRadius = config.unloadRadius * sectorSize * eval.radiusMultiplier;
                    float unloadRadiusSq = unloadWorldRadius * unloadWorldRadius;
                    // VK-1593: the SAME predicted metric the ring pass used. With a
                    // current-position-only test here, a sector pulled inside loadRadius by the
                    // lookahead while sitting beyond unloadRadius of the current position would
                    // load and unload on alternate frames.
                    float distSq = sourceDistanceSq(eval, *it, sectorSize);

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

        // VK-1593: consume the burst window opened by buildSourceEvals. burstActive is what
        // isBursting() reports and it survives the decrement, so the service - which reads the
        // burst state AFTER update() returns to size its entity budget - sees the same answer
        // this frame. Reading the counter there would miss a burstFrames == 1 window entirely.
        burstActive = burstFramesRemaining > 0;
        if (burstFramesRemaining > 0)
            --burstFramesRemaining;

        // Only activation bursts. Prefetch already has its own counter, and bursting unloads
        // would only evict faster - the opposite of what a camera jump needs.
        const int activateLimit = burstActive ? effectiveBurstLoads(config)
                                              : config.maxLoadsPerFrame;

        // Apply per-frame budgets. Prefetches draw on their own counter so a wide prefetch ring
        // can never eat the activation budget.
        streaming::FrameBudget activateBudget(activateLimit);
        streaming::FrameBudget prefetchBudget(config.maxPrefetchesPerFrame);
        streaming::FrameBudget unloadBudget(config.maxUnloadsPerFrame);

        outActions.reserve(
            std::min(static_cast<int>(unloadCandidates.size()), config.maxUnloadsPerFrame) +
            std::min(static_cast<int>(activateCandidates.size()), activateLimit) +
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

    float SectorStreamer::sourceDistanceSq(const SourceEval& eval, const SectorCoord& coord,
                                           float sectorWorldSize) const
    {
        const float current = sectorDistanceSq(coord, eval.position, sectorWorldSize);
        // Fast path AND exactness guarantee: with the lookahead off (or suppressed by the
        // teleport guard) predicted is a bit-for-bit copy of position, so every pre-VK-1593
        // caller gets the identical float this function used to return.
        if (eval.predicted == eval.position)
            return current;

        return std::min(current, sectorDistanceSq(coord, eval.predicted, sectorWorldSize));
    }

} // namespace world

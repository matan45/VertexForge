#include "HLODStreamer.hpp"
#include <algorithm>
#include <cmath>

namespace world
{
    namespace
    {
        // Which sources may pull VISIBLE proxy geometry.
        //
        // An HLOD proxy is rendered geometry, so only a source that actually wants entities counts.
        // SectorStreamer expresses the same rule as a CAP - it clamps each request down to the
        // source's targetState (`if (want > eval.targetState) want = eval.targetState`) - but a
        // proxy has nothing to clamp to: it is either drawn or it is not. So here it is a filter.
        //
        //   * Unloaded   - the source wants nothing. SectorStreamer::buildSourceEvals and
        //                  WorldSectorServiceImpl::poolPriorityAt both already skip these; this was
        //                  the one place that did not.
        //   * Prefetched - "bytes resident in memory, NO entities spawn", the documented contract
        //                  of registerWorldSourceEx targetState 1. Building a proxy ring around a
        //                  minimap hover pops terrain-scale geometry into view at a place the
        //                  player has never been - the exact opposite of what the caller asked for.
        [[nodiscard]] bool sourceWantsVisibleGeometry(const StreamingSource& source) noexcept
        {
            return source.targetState == SectorTargetState::Activated;
        }
    }

    void HLODStreamer::setConfig(const SectorStreamingConfig& streamCfg, const HLODConfig& hlodCfg)
    {
        streamConfig = streamCfg;
        hlodConfig = hlodCfg;
    }

    void HLODStreamer::clear()
    {
        loadedProxies.clear();
    }

    float HLODStreamer::cellDistanceSq(const HLODCellCoord& cell, uint8_t cellSize,
                                        const glm::vec3& pos, float sectorWorldSize) const
    {
        float cs = static_cast<float>(cellSize);
        float centerX = (static_cast<float>(cell.x) * cs + cs * 0.5f) * sectorWorldSize;
        float centerZ = (static_cast<float>(cell.z) * cs + cs * 0.5f) * sectorWorldSize;
        float dx = centerX - pos.x;
        float dz = centerZ - pos.z;
        return dx * dx + dz * dz;
    }

    bool HLODStreamer::anySectorLoaded(const HLODCellCoord& cell, const HLODTierConfig& tier,
                                        const WorldSectorManager& manager) const
    {
        int32_t cs = static_cast<int32_t>(tier.cellSize);
        int32_t baseX = cell.x * cs;
        int32_t baseZ = cell.z * cs;

        for (int32_t dx = 0; dx < cs; ++dx)
        {
            for (int32_t dz = 0; dz < cs; ++dz)
            {
                SectorCoord sc(baseX + dx, baseZ + dz);
                const auto* sector = manager.getSector(sc);
                // VK-1591: deliberately NOT extended to Prefetching/Prefetched. Those states hold
                // bytes and render nothing, so suppressing their HLOD proxy would punch a visible
                // hole in the prefetch ring.
                if (sector && (sector->state == SectorState::Loaded || sector->state == SectorState::Loading))
                {
                    return true;
                }
            }
        }
        return false;
    }

    void HLODStreamer::update(
        const std::vector<StreamingSource>& sources,
        const WorldSectorManager& manager,
        const SectorConfig& sectorConfig,
        std::vector<HLODStreamingAction>& outActions)
    {
        if (!hlodConfig.enabled || hlodConfig.tiers.empty() || sources.empty())
            return;

        float sectorSize = sectorConfig.sectorWorldSize;

        // VK-1594: tiers occupy ANNULI, not nested discs. Previously every tier's inner bound was
        // unloadRadius, so a cell 8 sectors out satisfied tier 0 (<=10), tier 1 (<=20) AND tier 2
        // (<=40) at once and all three proxies stacked on the same geometry. Tier N now begins
        // where tier N-1 ends. Ordered by displayRadius rather than trusting the tier ids to be
        // sorted, since .vfworld stores the tier table verbatim.
        std::vector<const HLODTierConfig*> ordered;
        ordered.reserve(hlodConfig.tiers.size());
        for (const auto& t : hlodConfig.tiers)
            ordered.push_back(&t);
        std::sort(ordered.begin(), ordered.end(),
                  [](const HLODTierConfig* a, const HLODTierConfig* b)
                  { return a->displayRadius < b->displayRadius; });

        for (size_t tierIndex = 0; tierIndex < ordered.size(); ++tierIndex)
        {
            const HLODTierConfig& tier = *ordered[tierIndex];

            float innerRadius = streamConfig.unloadRadius;
            if (tierIndex > 0)
                innerRadius = std::max(innerRadius, ordered[tierIndex - 1]->displayRadius);

            float innerRadiusWorld = innerRadius * sectorSize;
            float innerRadiusSq = innerRadiusWorld * innerRadiusWorld;
            float tierRadiusWorld = tier.displayRadius * sectorSize;
            float tierRadiusSq = tierRadiusWorld * tierRadiusWorld;

            // Hysteresis widens the band by one sector at BOTH ends. The outer margin stops thrash
            // as the camera retreats; the inner margin is what gives the tier N -> N+1 handoff its
            // crossfade overlap, and before VK-1594 it did not exist: a proxy the camera moved
            // INSIDE of dropped out of wantLoaded but still failed the beyond-outer-radius unload
            // test, so it stayed resident forever.
            float outerHysteresis = (tier.displayRadius + 1.0f) * sectorSize;
            float outerHysteresisSq = outerHysteresis * outerHysteresis;
            float innerHysteresis = std::max(0.0f, innerRadius - 1.0f) * sectorSize;
            float innerHysteresisSq = innerHysteresis * innerHysteresis;

            // Clamped: a cellSize of 0 from a hand-edited .vfworld would divide by zero below and
            // blow scanRange up to the VK-1588 runaway scan box.
            uint8_t cs = static_cast<uint8_t>(effectiveCellSize(tier));

            // Determine cells that should be loaded
            std::unordered_set<HLODCellCoord, HLODCellCoordHash> wantLoaded;

            for (const auto& source : sources)
            {
                if (!sourceWantsVisibleGeometry(source))
                    continue;

                float radius = tier.displayRadius * source.radiusMultiplier;
                int32_t scanRange = static_cast<int32_t>(std::ceil(radius / static_cast<float>(cs))) + 1;

                // Center cell for this source
                int32_t centerCellX = static_cast<int32_t>(std::floor(source.position.x / (sectorSize * cs)));
                int32_t centerCellZ = static_cast<int32_t>(std::floor(source.position.z / (sectorSize * cs)));

                for (int32_t dx = -scanRange; dx <= scanRange; ++dx)
                {
                    for (int32_t dz = -scanRange; dz <= scanRange; ++dz)
                    {
                        HLODCellCoord cell(centerCellX + dx, centerCellZ + dz, tier.tier);
                        float distSq = cellDistanceSq(cell, cs, source.position, sectorSize);

                        // Must fall inside this tier's annulus: past the previous tier's reach
                        // (or unloadRadius for the nearest tier) and within this tier's radius
                        if (distSq < innerRadiusSq || distSq > tierRadiusSq)
                            continue;

                        // Don't show proxy if any of its sectors are loaded
                        if (anySectorLoaded(cell, tier, manager))
                            continue;

                        wantLoaded.insert(cell);
                    }
                }
            }

            // Load new proxies
            for (const auto& cell : wantLoaded)
            {
                if (loadedProxies.find(cell) == loadedProxies.end())
                {
                    outActions.push_back({cell, true});
                    loadedProxies.insert(cell);
                }
            }

            // Unload proxies that are no longer wanted
            std::vector<HLODCellCoord> toUnload;
            for (const auto& loaded : loadedProxies)
            {
                if (loaded.tier != tier.tier) continue;

                if (wantLoaded.find(loaded) == wantLoaded.end())
                {
                    // Keep the proxy while at least one source still sees it inside the
                    // hysteresis-widened annulus - whether it fell out through the near edge
                    // (handing off to a finer tier) or the far edge (handing off to a coarser one).
                    bool keptByAnySource = false;
                    for (const auto& source : sources)
                    {
                        // Same filter as the want pass: a source that cannot ask for a proxy
                        // cannot keep one alive either, or a prefetch-only source would pin
                        // geometry it was never allowed to request.
                        if (!sourceWantsVisibleGeometry(source))
                            continue;

                        float distSq = cellDistanceSq(loaded, cs, source.position, sectorSize);
                        if (distSq >= innerHysteresisSq && distSq <= outerHysteresisSq)
                        {
                            keptByAnySource = true;
                            break;
                        }
                    }

                    // Also unload if any sector in this cell is now loaded
                    bool sectorLoaded = anySectorLoaded(loaded, tier, manager);

                    if (!keptByAnySource || sectorLoaded)
                    {
                        toUnload.push_back(loaded);
                    }
                }
            }

            for (const auto& cell : toUnload)
            {
                outActions.push_back({cell, false});
                loadedProxies.erase(cell);
            }
        }
    }

} // namespace world

#include "HLODStreamer.hpp"
#include <algorithm>
#include <cmath>

namespace world
{
    void HLODStreamer::setConfig(const SectorStreamingConfig& streamCfg, const HLODConfig& hlodCfg)
    {
        streamConfig = streamCfg;
        hlodConfig = hlodCfg;
    }

    void HLODStreamer::clear()
    {
        loadedProxies.clear();
    }

    HLODCellCoord HLODStreamer::sectorToCell(const SectorCoord& coord, uint8_t cellSize) const
    {
        int32_t cs = static_cast<int32_t>(cellSize);
        // Floor division for negative coords
        int32_t cx = (coord.x >= 0) ? coord.x / cs : (coord.x - cs + 1) / cs;
        int32_t cz = (coord.z >= 0) ? coord.z / cs : (coord.z - cs + 1) / cs;
        return HLODCellCoord(cx, cz, 0);
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
        float unloadRadiusWorld = streamConfig.unloadRadius * sectorSize;
        float unloadRadiusSq = unloadRadiusWorld * unloadRadiusWorld;

        for (const auto& tier : hlodConfig.tiers)
        {
            float tierRadiusWorld = tier.displayRadius * sectorSize;
            float tierRadiusSq = tierRadiusWorld * tierRadiusWorld;
            float hysteresisRadiusSq = (tier.displayRadius + 1.0f) * sectorSize;
            hysteresisRadiusSq *= hysteresisRadiusSq;

            uint8_t cs = tier.cellSize;

            // Determine cells that should be loaded
            std::unordered_set<HLODCellCoord, HLODCellCoordHash> wantLoaded;

            for (const auto& source : sources)
            {
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

                        // Must be beyond unload radius but within tier display radius
                        if (distSq < unloadRadiusSq || distSq > tierRadiusSq)
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
                    // Check hysteresis: only unload if beyond hysteresis radius from ALL sources
                    bool beyondAll = true;
                    for (const auto& source : sources)
                    {
                        float distSq = cellDistanceSq(loaded, cs, source.position, sectorSize);
                        if (distSq <= hysteresisRadiusSq)
                        {
                            beyondAll = false;
                            break;
                        }
                    }

                    // Also unload if any sector in this cell is now loaded
                    bool sectorLoaded = anySectorLoaded(loaded, tier, manager);

                    if (beyondAll || sectorLoaded)
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

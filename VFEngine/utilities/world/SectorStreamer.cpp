#include "SectorStreamer.hpp"
#include <algorithm>
#include <cmath>

namespace world
{
    SectorStreamer::SectorStreamer(const SectorStreamingConfig& config)
        : config(config)
    {
    }

    void SectorStreamer::setConfig(const SectorStreamingConfig& config)
    {
        this->config = config;
        if (this->config.unloadRadius <= this->config.loadRadius)
            this->config.unloadRadius = this->config.loadRadius + 1.0f;
    }

    void SectorStreamer::setEnabled(bool value)
    {
        enabled = value;
        if (value)
            needsSeed = true;
    }

    void SectorStreamer::seedLoadedSectors(const WorldSectorManager& manager)
    {
        loadedSectors.clear();
        manager.forEachSector([&](const WorldSector& sector)
        {
            if (sector.state == SectorState::Loaded || sector.state == SectorState::Loading)
                loadedSectors.insert(sector.coord);
        });
        needsSeed = false;
    }

    void SectorStreamer::update(
        const glm::vec3& cameraPos,
        const WorldSectorManager& manager,
        std::vector<SectorStreamingAction>& outActions)
    {
        outActions.clear();

        if (!enabled)
            return;

        if (needsSeed)
            seedLoadedSectors(manager);

        float sectorSize = manager.getConfig().sectorWorldSize;
        float loadWorldRadius = config.loadRadius * sectorSize;
        float unloadWorldRadius = config.unloadRadius * sectorSize;
        float loadRadiusSq = loadWorldRadius * loadWorldRadius;
        float unloadRadiusSq = unloadWorldRadius * unloadWorldRadius;

        loadCandidates.clear();
        unloadCandidates.clear();

        // Load candidates: scan coordinate range around camera
        int minX = static_cast<int>(std::floor((cameraPos.x - loadWorldRadius) / sectorSize));
        int maxX = static_cast<int>(std::floor((cameraPos.x + loadWorldRadius) / sectorSize));
        int minZ = static_cast<int>(std::floor((cameraPos.z - loadWorldRadius) / sectorSize));
        int maxZ = static_cast<int>(std::floor((cameraPos.z + loadWorldRadius) / sectorSize));

        for (int x = minX; x <= maxX; ++x)
        {
            for (int z = minZ; z <= maxZ; ++z)
            {
                SectorCoord coord{x, z};
                const WorldSector* sector = manager.getSector(coord);
                if (!sector) continue;

                // Sync: track any loaded/loading sectors we encounter
                if (sector->state == SectorState::Loaded || sector->state == SectorState::Loading)
                    loadedSectors.insert(coord);

                float distSq = sectorDistanceSq(coord, cameraPos, sectorSize);
                if (distSq > loadRadiusSq)
                    continue;

                if (sector->state == SectorState::Unloaded && !sector->filePath.empty())
                {
                    loadCandidates.push_back({coord, distSq});
                }
            }
        }

        // Unload candidates: iterate only tracked loaded sectors
        auto it = loadedSectors.begin();
        while (it != loadedSectors.end())
        {
            const WorldSector* sector = manager.getSector(*it);
            if (!sector || sector->state == SectorState::Unloaded)
            {
                it = loadedSectors.erase(it);
                continue;
            }

            if (sector->state == SectorState::Loaded)
            {
                float distSq = sectorDistanceSq(*it, cameraPos, sectorSize);
                if (distSq > unloadRadiusSq && !sector->dirty)
                {
                    unloadCandidates.push_back({*it, distSq});
                }
            }
            ++it;
        }

        // Sort: load nearest first, unload farthest first
        std::sort(loadCandidates.begin(), loadCandidates.end(),
                  [](const Candidate& a, const Candidate& b) { return a.distSq < b.distSq; });

        std::sort(unloadCandidates.begin(), unloadCandidates.end(),
                  [](const Candidate& a, const Candidate& b) { return a.distSq > b.distSq; });

        // Apply budget
        int loadCount = std::min(static_cast<int>(loadCandidates.size()), config.maxLoadsPerFrame);
        int unloadCount = std::min(static_cast<int>(unloadCandidates.size()), config.maxUnloadsPerFrame);

        outActions.reserve(loadCount + unloadCount);

        for (int i = 0; i < unloadCount; ++i)
        {
            loadedSectors.erase(unloadCandidates[i].coord);
            outActions.push_back({unloadCandidates[i].coord, false});
        }

        for (int i = 0; i < loadCount; ++i)
        {
            loadedSectors.insert(loadCandidates[i].coord);
            outActions.push_back({loadCandidates[i].coord, true});
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

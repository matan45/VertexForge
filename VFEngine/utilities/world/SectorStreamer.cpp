#include "SectorStreamer.hpp"
#include <algorithm>

namespace world
{
    SectorStreamer::SectorStreamer(const SectorStreamingConfig& config)
        : config(config)
    {
    }

    void SectorStreamer::setConfig(const SectorStreamingConfig& config)
    {
        this->config = config;
        if (this->config.unloadRadius < this->config.loadRadius)
            this->config.unloadRadius = this->config.loadRadius * 1.25f;
    }

    void SectorStreamer::update(
        const glm::vec3& cameraPos,
        const WorldSectorManager& manager,
        std::vector<SectorStreamingAction>& outActions)
    {
        outActions.clear();

        if (!enabled)
            return;

        float sectorSize = manager.getConfig().sectorWorldSize;
        float loadRadiusSq = config.loadRadius * config.loadRadius;
        float unloadRadiusSq = config.unloadRadius * config.unloadRadius;

        loadCandidates.clear();
        unloadCandidates.clear();

        // Collect load/unload candidates by iterating all known sectors
        manager.forEachSector([&](const WorldSector& sector)
        {
            float distSq = sectorDistanceSq(sector.coord, cameraPos, sectorSize);

            if (sector.state == SectorState::Unloaded && !sector.filePath.empty())
            {
                if (distSq <= loadRadiusSq)
                {
                    loadCandidates.push_back({sector.coord, distSq});
                }
            }
            else if (sector.state == SectorState::Loaded)
            {
                if (distSq > unloadRadiusSq && !sector.dirty)
                {
                    unloadCandidates.push_back({sector.coord, distSq});
                }
            }
        });

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
            outActions.push_back({unloadCandidates[i].coord, false});
        }

        for (int i = 0; i < loadCount; ++i)
        {
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

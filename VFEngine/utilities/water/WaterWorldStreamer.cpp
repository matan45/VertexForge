#include "WaterWorldStreamer.hpp"
#include "WaterGrid.hpp"
#include <algorithm>

namespace water
{
    WaterWorldStreamer::WaterWorldStreamer(const WaterStreamingConfig& config)
        : config(config)
    {
    }

    void WaterWorldStreamer::setConfig(const WaterStreamingConfig& config)
    {
        this->config = config;
        if (this->config.unloadRadius < this->config.loadRadius)
            this->config.unloadRadius = this->config.loadRadius * 1.25f;
    }

    void WaterWorldStreamer::update(
        const glm::vec3& cameraPos,
        float worldTileSize,
        const WaterDefinitionMap& definitions,
        const WaterGrid& grid,
        std::vector<WaterStreamingAction>& outActions)
    {
        outActions.clear();

        if (!enabled)
            return;

        float loadRadiusSq = config.loadRadius * config.loadRadius;
        float unloadRadiusSq = config.unloadRadius * config.unloadRadius;

        loadCandidates.clear();
        unloadCandidates.clear();

        // Collect load candidates: defined but not in grid, within load radius
        definitions.forEachDefinition([&](const TileCoord& coord, const WaterTileDefinition&)
        {
            if (grid.hasTile(coord))
                return;

            float distSq = tileDistanceSq(coord, cameraPos, worldTileSize);
            if (distSq <= loadRadiusSq)
            {
                loadCandidates.push_back({coord, distSq});
            }
        });

        // Collect unload candidates: in grid, beyond unload radius
        for (const auto* tile : grid.getAllTiles())
        {
            if (!tile)
                continue;

            float distSq = tileDistanceSq(tile->coord, cameraPos, worldTileSize);
            if (distSq > unloadRadiusSq)
            {
                unloadCandidates.push_back({tile->coord, distSq});
            }
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
            outActions.push_back({unloadCandidates[i].coord, false});
        }

        for (int i = 0; i < loadCount; ++i)
        {
            outActions.push_back({loadCandidates[i].coord, true});
        }
    }

    float WaterWorldStreamer::tileDistanceSq(const TileCoord& coord, const glm::vec3& cameraPos,
                                             float worldTileSize) const
    {
        float tileCenterX = (static_cast<float>(coord.x) + 0.5f) * worldTileSize;
        float tileCenterZ = (static_cast<float>(coord.z) + 0.5f) * worldTileSize;

        float dx = tileCenterX - cameraPos.x;
        float dz = tileCenterZ - cameraPos.z;

        return dx * dx + dz * dz;
    }
}

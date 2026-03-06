#include "TerrainWorldStreamer.hpp"
#include <algorithm>

namespace terrain
{
    TerrainWorldStreamer::TerrainWorldStreamer(const StreamingConfig& config)
        : config(config)
    {
    }

    void TerrainWorldStreamer::setConfig(const StreamingConfig& config)
    {
        this->config = config;
        if (this->config.unloadRadius < this->config.loadRadius)
            this->config.unloadRadius = this->config.loadRadius * 1.25f;
    }

    std::vector<StreamingAction> TerrainWorldStreamer::update(
        const glm::vec3& cameraPos,
        float worldTileSize,
        const TerrainFileCache& fileCache,
        const TerrainGrid& grid)
    {
        std::vector<StreamingAction> actions;

        if (!enabled)
            return actions;

        float loadRadiusSq = config.loadRadius * config.loadRadius;
        float unloadRadiusSq = config.unloadRadius * config.unloadRadius;

        // Collect load candidates: available on disk, not in grid, within load radius
        struct Candidate
        {
            TileCoord coord;
            float distSq;
        };

        std::vector<Candidate> loadCandidates;
        std::vector<Candidate> unloadCandidates;

        auto availableCoords = fileCache.getAvailableCoords();
        for (const auto& coord : availableCoords)
        {
            if (grid.hasTile(coord))
                continue;

            float distSq = tileDistanceSq(coord, cameraPos, worldTileSize);
            if (distSq <= loadRadiusSq)
            {
                loadCandidates.push_back({coord, distSq});
            }
        }

        // Collect unload candidates: in grid, beyond unload radius
        auto allTiles = grid.getAllTiles();
        for (const auto* tile : allTiles)
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

        actions.reserve(loadCount + unloadCount);

        for (int i = 0; i < unloadCount; ++i)
        {
            actions.push_back({unloadCandidates[i].coord, false});
        }

        for (int i = 0; i < loadCount; ++i)
        {
            actions.push_back({loadCandidates[i].coord, true});
        }

        return actions;
    }

    float TerrainWorldStreamer::tileDistanceSq(const TileCoord& coord, const glm::vec3& cameraPos,
                                               float worldTileSize) const
    {
        float tileCenterX = (static_cast<float>(coord.x) + 0.5f) * worldTileSize;
        float tileCenterZ = (static_cast<float>(coord.z) + 0.5f) * worldTileSize;

        float dx = tileCenterX - cameraPos.x;
        float dz = tileCenterZ - cameraPos.z;

        return dx * dx + dz * dz;
    }

} // namespace terrain

#include "NavmeshStreamer.hpp"
#include <algorithm>
#include <cmath>

namespace services
{
    NavmeshStreamer::NavmeshStreamer() = default;

    void NavmeshStreamer::setConfig(const ::events::navmesh::NavmeshStreamingConfig& cfg)
    {
        config = cfg;
    }

    float NavmeshStreamer::tileDistanceSq(const navigation::NavmeshTileCoord& coord,
                                            const glm::vec3& cameraPos) const
    {
        float tileWorldSize = bakeSettings.tileSize * bakeSettings.cellSize;
        float centerX = (coord.x + 0.5f) * tileWorldSize;
        float centerZ = (coord.z + 0.5f) * tileWorldSize;

        float dx = cameraPos.x - centerX;
        float dz = cameraPos.z - centerZ;
        return dx * dx + dz * dz;
    }

    void NavmeshStreamer::update(const glm::vec3& cameraPos,
                                  std::vector<navigation::NavmeshTileCoord>& outLoaded,
                                  std::vector<navigation::NavmeshTileCoord>& outUnloaded)
    {
        if (!enabled || !tileCache || !navmeshProvider)
            return;

        float loadRadiusSq = config.loadRadius * config.loadRadius;
        float unloadRadiusSq = config.unloadRadius * config.unloadRadius;

        // Find tiles to unload
        unloadCandidates.clear();
        for (const auto& coord : loadedTiles)
        {
            float distSq = tileDistanceSq(coord, cameraPos);
            if (distSq > unloadRadiusSq)
            {
                unloadCandidates.push_back({coord, distSq});
            }
        }

        // Sort farthest first
        std::sort(unloadCandidates.begin(), unloadCandidates.end(),
            [](const Candidate& a, const Candidate& b) { return a.distSq > b.distSq; });

        int unloaded = 0;
        for (const auto& candidate : unloadCandidates)
        {
            if (unloaded >= config.maxUnloadsPerFrame)
                break;

            navmeshProvider->removeNavmeshTile(candidate.coord.x, candidate.coord.z);
            loadedTiles.erase(candidate.coord);
            outUnloaded.push_back(candidate.coord);
            unloaded++;
        }

        // Find tiles to load
        loadCandidates.clear();
        tileCache->forEachTile([&](const navigation::NavmeshTileCoord& coord)
        {
            if (loadedTiles.count(coord))
                return;

            float distSq = tileDistanceSq(coord, cameraPos);
            if (distSq <= loadRadiusSq)
            {
                loadCandidates.push_back({coord, distSq});
            }
        });

        // Sort nearest first
        std::sort(loadCandidates.begin(), loadCandidates.end(),
            [](const Candidate& a, const Candidate& b) { return a.distSq < b.distSq; });

        int loaded = 0;
        for (const auto& candidate : loadCandidates)
        {
            if (loaded >= config.maxLoadsPerFrame)
                break;

            navigation::NavmeshTileData tileData;
            if (tileCache->loadTile(candidate.coord, tileData))
            {
                if (navmeshProvider->addNavmeshTile(tileData))
                {
                    loadedTiles.insert(candidate.coord);
                    outLoaded.push_back(candidate.coord);
                    loaded++;
                }
            }
        }
    }

    bool NavmeshStreamer::isTileLoaded(const navigation::NavmeshTileCoord& coord) const
    {
        return loadedTiles.count(coord) > 0;
    }

    void NavmeshStreamer::clear()
    {
        loadedTiles.clear();
    }
}

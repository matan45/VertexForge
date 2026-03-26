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
                                            const glm::vec3& pos) const
    {
        float tileWorldSize = bakeSettings.tileSize * bakeSettings.cellSize;
        float centerX = (coord.x + 0.5f) * tileWorldSize;
        float centerZ = (coord.z + 0.5f) * tileWorldSize;

        float dx = pos.x - centerX;
        float dz = pos.z - centerZ;
        return dx * dx + dz * dz;
    }

    float NavmeshStreamer::minDistanceToSources(const navigation::NavmeshTileCoord& coord,
                                                   const std::vector<StreamingSource>& sources) const
    {
        float minDist = std::numeric_limits<float>::max();
        for (const auto& src : sources)
        {
            float d = tileDistanceSq(coord, src.position);
            if (d < minDist)
                minDist = d;
        }
        return minDist;
    }

    bool NavmeshStreamer::isWithinAnySource(const navigation::NavmeshTileCoord& coord,
                                              const std::vector<StreamingSource>& sources,
                                              bool useUnloadRadius) const
    {
        for (const auto& src : sources)
        {
            float distSq = tileDistanceSq(coord, src.position);
            float radiusSq = useUnloadRadius ? src.unloadRadiusSq : src.loadRadiusSq;
            if (distSq <= radiusSq)
                return true;
        }
        return false;
    }

    // Single-position backward compat wrapper
    void NavmeshStreamer::update(const glm::vec3& cameraPos,
                                  std::vector<navigation::NavmeshTileCoord>& outLoaded,
                                  std::vector<navigation::NavmeshTileCoord>& outUnloaded)
    {
        std::vector<navigation::NavmeshTileCoord> unused;
        std::vector<StreamingSource> sources;
        sources.push_back({cameraPos,
                           config.loadRadius * config.loadRadius,
                           config.unloadRadius * config.unloadRadius});
        update(sources, outLoaded, outUnloaded, unused);
    }

    // Multi-source update with on-demand generation detection
    void NavmeshStreamer::update(const std::vector<StreamingSource>& sources,
                                  std::vector<navigation::NavmeshTileCoord>& outLoaded,
                                  std::vector<navigation::NavmeshTileCoord>& outUnloaded,
                                  std::vector<navigation::NavmeshTileCoord>& outNeedGeneration)
    {
        if (!enabled || !navmeshProvider || sources.empty())
            return;

        // === UNLOAD: tiles beyond ALL sources' unload radii ===
        unloadCandidates.clear();
        for (const auto& coord : loadedTiles)
        {
            if (!isWithinAnySource(coord, sources, true))
            {
                float minDist = minDistanceToSources(coord, sources);
                unloadCandidates.push_back({coord, minDist});
            }
        }

        std::sort(unloadCandidates.begin(), unloadCandidates.end(),
            [](const Candidate& a, const Candidate& b) { return a.distSq > b.distSq; });

        int unloaded = 0;
        for (const auto& candidate : unloadCandidates)
        {
            if (unloaded >= config.maxUnloadsPerFrame)
                break;

            navmeshProvider->removeNavmeshTile(candidate.coord.x, candidate.coord.z);
            loadedTiles.erase(candidate.coord);
            generatedTiles.erase(candidate.coord);
            outUnloaded.push_back(candidate.coord);
            unloaded++;
        }

        // === LOAD: cached tiles within ANY source's load radius ===
        loadCandidates.clear();
        if (tileCache)
        {
            tileCache->forEachTile([&](const navigation::NavmeshTileCoord& coord)
            {
                if (loadedTiles.count(coord))
                    return;

                if (isWithinAnySource(coord, sources, false))
                {
                    float minDist = minDistanceToSources(coord, sources);
                    loadCandidates.push_back({coord, minDist});
                }
            });
        }

        std::sort(loadCandidates.begin(), loadCandidates.end(),
            [](const Candidate& a, const Candidate& b) { return a.distSq < b.distSq; });

        int loaded = 0;
        for (const auto& candidate : loadCandidates)
        {
            if (loaded >= config.maxLoadsPerFrame)
                break;

            navigation::NavmeshTileData tileData;
            if (tileCache && tileCache->loadTile(candidate.coord, tileData))
            {
                if (navmeshProvider->addNavmeshTile(tileData))
                {
                    loadedTiles.insert(candidate.coord);
                    outLoaded.push_back(candidate.coord);
                    loaded++;
                }
            }
        }

        // === GENERATION: tiles within load radius but NOT in cache and NOT loaded ===
        float tileWorldSize = bakeSettings.tileSize * bakeSettings.cellSize;
        if (tileWorldSize <= 0.0f)
            return;

        std::unordered_set<navigation::NavmeshTileCoord, navigation::NavmeshTileCoordHash> needed;

        for (const auto& src : sources)
        {
            float loadRadius = std::sqrt(src.loadRadiusSq);
            int minTX = static_cast<int>(std::floor((src.position.x - loadRadius) / tileWorldSize));
            int maxTX = static_cast<int>(std::floor((src.position.x + loadRadius) / tileWorldSize));
            int minTZ = static_cast<int>(std::floor((src.position.z - loadRadius) / tileWorldSize));
            int maxTZ = static_cast<int>(std::floor((src.position.z + loadRadius) / tileWorldSize));

            for (int tx = minTX; tx <= maxTX; ++tx)
            {
                for (int tz = minTZ; tz <= maxTZ; ++tz)
                {
                    navigation::NavmeshTileCoord coord{tx, tz};
                    float distSq = tileDistanceSq(coord, src.position);
                    if (distSq > src.loadRadiusSq)
                        continue;

                    if (loadedTiles.count(coord))
                        continue;

                    if (tileCache && tileCache->hasTile(coord))
                        continue;

                    needed.insert(coord);
                }
            }
        }

        for (const auto& coord : needed)
            outNeedGeneration.push_back(coord);
    }

    bool NavmeshStreamer::isTileLoaded(const navigation::NavmeshTileCoord& coord) const
    {
        return loadedTiles.count(coord) > 0;
    }

    void NavmeshStreamer::clear()
    {
        loadedTiles.clear();
        generatedTiles.clear();
    }
}

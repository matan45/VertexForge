#include "NavmeshStreamer.hpp"
#include "../../events/EventDispatcher.hpp"
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

    uint8_t NavmeshStreamer::determineLod(float distSq) const
    {
        if (lodConfig.lodCount <= 1)
            return 0;

        // Check from finest to coarsest
        for (uint8_t i = 0; i < lodConfig.lodCount; ++i)
        {
            float threshold = config.lodDistances[i];
            if (distSq <= threshold * threshold)
                return i;
        }
        // Beyond all thresholds, use coarsest
        return static_cast<uint8_t>(lodConfig.lodCount - 1);
    }

    bool NavmeshStreamer::isLodTransitionValid(const navigation::NavmeshTileCoord& coord, uint8_t targetLod) const
    {
        // Check 4-neighbors: LOD difference must be <= 1
        const navigation::NavmeshTileCoord neighbors[4] = {
            {coord.x - 1, coord.z},
            {coord.x + 1, coord.z},
            {coord.x, coord.z - 1},
            {coord.x, coord.z + 1}
        };

        for (const auto& n : neighbors)
        {
            auto it = loadedTileLods.find(n);
            if (it != loadedTileLods.end())
            {
                int diff = static_cast<int>(targetLod) - static_cast<int>(it->second);
                if (diff > 1 || diff < -1)
                    return false;
            }
        }
        return true;
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
        for (const auto& [coord, lod] : loadedTileLods)
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
            loadedTileLods.erase(candidate.coord);
            generatedTiles.erase(candidate.coord);
            outUnloaded.push_back(candidate.coord);
            unloaded++;
        }

        // === LOD TRANSITION: swap tiles that need a different LOD ===
        if (lodConfig.lodCount > 1)
        {
            struct LodTransition
            {
                navigation::NavmeshTileCoord coord;
                uint8_t currentLod;
                uint8_t targetLod;
                float distSq;
            };
            std::vector<LodTransition> transitions;

            for (const auto& [coord, currentLod] : loadedTileLods)
            {
                float minDist = minDistanceToSources(coord, sources);
                uint8_t targetLod = determineLod(minDist);

                if (targetLod != currentLod && isLodTransitionValid(coord, targetLod))
                {
                    transitions.push_back({coord, currentLod, targetLod, minDist});
                }
            }

            // Sort by distance (closest first - prioritize high-detail transitions)
            std::sort(transitions.begin(), transitions.end(),
                [](const LodTransition& a, const LodTransition& b) { return a.distSq < b.distSq; });

            int transitioned = 0;
            for (const auto& t : transitions)
            {
                if (transitioned >= config.maxLoadsPerFrame)
                    break;

                // Try to load the new LOD from cache
                navigation::NavmeshTileLodKey lodKey{t.coord.x, t.coord.z, t.targetLod};
                navigation::NavmeshTileData tileData;
                bool loaded = false;

                if (tileCache)
                {
                    loaded = tileCache->loadTile(lodKey, tileData);
                }

                if (loaded)
                {
                    // Remove old tile, add new one
                    navmeshProvider->removeNavmeshTile(t.coord.x, t.coord.z);
                    if (navmeshProvider->addNavmeshTile(tileData))
                    {
                        loadedTileLods[t.coord] = t.targetLod;
                        outUnloaded.push_back(t.coord);
                        outLoaded.push_back(t.coord);
                        transitioned++;

                        // Publish LOD change notification
                        auto& dispatcher = ::events::EventDispatcher::instance();
                        ::events::navmesh::NavmeshTileLodChangedNotification notif;
                        notif.tileX = t.coord.x;
                        notif.tileZ = t.coord.z;
                        notif.oldLod = t.currentLod;
                        notif.newLod = t.targetLod;
                        dispatcher.publish(notif);
                    }
                }
            }
        }

        // === LOAD: cached tiles within ANY source's load radius ===
        loadCandidates.clear();
        if (tileCache)
        {
            tileCache->forEachTile([&](const navigation::NavmeshTileCoord& coord)
            {
                if (loadedTileLods.count(coord))
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

            // Select LOD based on distance + boundary constraint
            uint8_t targetLod = 0;
            if (lodConfig.lodCount > 1)
            {
                targetLod = determineLod(candidate.distSq);
                // Clamp to satisfy neighbor constraint
                if (!isLodTransitionValid(candidate.coord, targetLod))
                {
                    // Try lower LOD values until valid
                    while (targetLod > 0 && !isLodTransitionValid(candidate.coord, targetLod))
                        targetLod--;
                }
            }

            navigation::NavmeshTileData tileData;
            bool tileLoaded = false;

            if (tileCache)
            {
                if (lodConfig.lodCount > 1 && targetLod > 0)
                {
                    navigation::NavmeshTileLodKey lodKey{candidate.coord.x, candidate.coord.z, targetLod};
                    tileLoaded = tileCache->loadTile(lodKey, tileData);
                }

                // Fall back to LOD 0 if LOD tile not available
                if (!tileLoaded)
                {
                    tileLoaded = tileCache->loadTile(candidate.coord, tileData);
                    targetLod = 0;
                }
            }

            if (tileLoaded)
            {
                if (navmeshProvider->addNavmeshTile(tileData))
                {
                    loadedTileLods[candidate.coord] = targetLod;
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

                    if (loadedTileLods.count(coord))
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
        return loadedTileLods.count(coord) > 0;
    }

    void NavmeshStreamer::clear()
    {
        loadedTileLods.clear();
        generatedTiles.clear();
    }
}

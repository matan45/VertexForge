#pragma once

#include "navigation/NavmeshData.hpp"
#include "navigation/NavmeshTileCache.hpp"
#include "../../providers/navmesh/INavmeshProvider.hpp"
#include "../../events/navmesh/NavmeshEvents.hpp"
#include "types/NavmeshTypes.hpp"
#include <glm/glm.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace services
{
    struct StreamingSource
    {
        glm::vec3 position{0.0f};
        float loadRadiusSq = 0.0f;
        float unloadRadiusSq = 0.0f;
    };

    class NavmeshStreamer
    {
    private:
        struct Candidate
        {
            navigation::NavmeshTileCoord coord;
            float distSq;
        };

        ::events::navmesh::NavmeshStreamingConfig config;
        types::NavmeshBakeSettings bakeSettings;
        types::NavmeshLodConfig lodConfig;
        bool enabled = false;
        int maxResidentTiles = 0;

        navigation::NavmeshTileCache* tileCache = nullptr;
        INavmeshProvider* navmeshProvider = nullptr;

        std::unordered_map<navigation::NavmeshTileCoord, uint8_t, navigation::NavmeshTileCoordHash> loadedTileLods;
        std::unordered_set<navigation::NavmeshTileCoord, navigation::NavmeshTileCoordHash> generatedTiles;
        std::vector<Candidate> loadCandidates;
        std::vector<Candidate> unloadCandidates;
    public:
        explicit NavmeshStreamer();

        void setConfig(const ::events::navmesh::NavmeshStreamingConfig& config);
        [[nodiscard]] const ::events::navmesh::NavmeshStreamingConfig& getConfig() const { return config; }

        void setEnabled(bool value) { enabled = value; }
        [[nodiscard]] bool isEnabled() const { return enabled; }

        void setTileCache(navigation::NavmeshTileCache* cache) { tileCache = cache; }
        void setProvider(INavmeshProvider* provider) { navmeshProvider = provider; }
        void setSettings(const types::NavmeshBakeSettings& s) { bakeSettings = s; }
        void setLodConfig(const types::NavmeshLodConfig& cfg) { lodConfig = cfg; }

        // dtNavMesh resident budget (0 = unlimited): load candidates beyond this
        // are skipped nearest-first; dtNavMesh::addTile would fail past it anyway.
        void setMaxResidentTiles(int value) { maxResidentTiles = value; }

        // Single-position update (backward compat - uses global config radii)
        void update(const glm::vec3& cameraPos,
                    std::vector<navigation::NavmeshTileCoord>& outLoaded,
                    std::vector<navigation::NavmeshTileCoord>& outUnloaded);

        // Multi-source update with on-demand generation detection
        void update(const std::vector<StreamingSource>& sources,
                    std::vector<navigation::NavmeshTileCoord>& outLoaded,
                    std::vector<navigation::NavmeshTileCoord>& outUnloaded,
                    std::vector<navigation::NavmeshTileCoord>& outNeedGeneration);

        [[nodiscard]] bool isTileLoaded(const navigation::NavmeshTileCoord& coord) const;
        [[nodiscard]] const std::unordered_map<navigation::NavmeshTileCoord, uint8_t, navigation::NavmeshTileCoordHash>&
            getLoadedTileLods() const { return loadedTileLods; }

        void markTileLoaded(const navigation::NavmeshTileCoord& coord) { loadedTileLods[coord] = 0; }
        void markTileUnloaded(const navigation::NavmeshTileCoord& coord)
        {
            loadedTileLods.erase(coord);
            generatedTiles.erase(coord);
        }
        void markTileGenerated(const navigation::NavmeshTileCoord& coord) { generatedTiles.insert(coord); loadedTileLods[coord] = 0; }
        void clear();

    private:
        [[nodiscard]] uint8_t determineLod(float distSq) const;
        [[nodiscard]] bool isLodTransitionValid(const navigation::NavmeshTileCoord& coord, uint8_t targetLod) const;
        [[nodiscard]] float tileDistanceSq(const navigation::NavmeshTileCoord& coord,
                                            const glm::vec3& pos) const;
        [[nodiscard]] float minDistanceToSources(const navigation::NavmeshTileCoord& coord,
                                                   const std::vector<StreamingSource>& sources) const;
        [[nodiscard]] bool isWithinAnySource(const navigation::NavmeshTileCoord& coord,
                                              const std::vector<StreamingSource>& sources,
                                              bool useUnloadRadius) const;
    };
}

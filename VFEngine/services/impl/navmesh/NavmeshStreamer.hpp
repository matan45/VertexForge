#pragma once

#include "navigation/NavmeshData.hpp"
#include "navigation/NavmeshTileCache.hpp"
#include "../../providers/navmesh/INavmeshProvider.hpp"
#include "../../events/navmesh/NavmeshEvents.hpp"
#include <glm/glm.hpp>
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
        bool enabled = false;

        navigation::NavmeshTileCache* tileCache = nullptr;
        INavmeshProvider* navmeshProvider = nullptr;

        std::unordered_set<navigation::NavmeshTileCoord, navigation::NavmeshTileCoordHash> loadedTiles;
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
        [[nodiscard]] const std::unordered_set<navigation::NavmeshTileCoord, navigation::NavmeshTileCoordHash>&
            getLoadedTiles() const { return loadedTiles; }

        void markTileLoaded(const navigation::NavmeshTileCoord& coord) { loadedTiles.insert(coord); }
        void markTileGenerated(const navigation::NavmeshTileCoord& coord) { generatedTiles.insert(coord); loadedTiles.insert(coord); }
        void clear();

    private:
        [[nodiscard]] float tileDistanceSq(const navigation::NavmeshTileCoord& coord,
                                            const glm::vec3& pos) const;
        [[nodiscard]] float minDistanceToSources(const navigation::NavmeshTileCoord& coord,
                                                   const std::vector<StreamingSource>& sources) const;
        [[nodiscard]] bool isWithinAnySource(const navigation::NavmeshTileCoord& coord,
                                              const std::vector<StreamingSource>& sources,
                                              bool useUnloadRadius) const;
    };
}

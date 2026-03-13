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

        void update(const glm::vec3& cameraPos,
                    std::vector<navigation::NavmeshTileCoord>& outLoaded,
                    std::vector<navigation::NavmeshTileCoord>& outUnloaded);

        [[nodiscard]] bool isTileLoaded(const navigation::NavmeshTileCoord& coord) const;
        [[nodiscard]] const std::unordered_set<navigation::NavmeshTileCoord, navigation::NavmeshTileCoordHash>&
            getLoadedTiles() const { return loadedTiles; }

        void markTileLoaded(const navigation::NavmeshTileCoord& coord) { loadedTiles.insert(coord); }
        void clear();

    private:

        [[nodiscard]] float tileDistanceSq(const navigation::NavmeshTileCoord& coord,
                                            const glm::vec3& cameraPos) const;
    };
}

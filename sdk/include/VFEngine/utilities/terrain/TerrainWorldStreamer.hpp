#pragma once
#include "TerrainExport.hpp"

#include "TerrainTypes.hpp"
#include "TerrainFileCache.hpp"
#include "TerrainGrid.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace terrain
{
    struct StreamingConfig
    {
        float loadRadius = 512.0f;
        float unloadRadius = 640.0f;
        int maxLoadsPerFrame = 4;
        int maxUnloadsPerFrame = 4;
    };

    struct StreamingAction
    {
        TileCoord coord;
        bool isLoad = true;
    };

#pragma warning(push)
#pragma warning(disable: 4251)
    class VF_TERRAIN_API TerrainWorldStreamer
    {
    public:
        explicit TerrainWorldStreamer(const StreamingConfig& config = {});

        void setConfig(const StreamingConfig& config);
        [[nodiscard]] const StreamingConfig& getConfig() const { return config; }

        void update(
            const glm::vec3& cameraPos,
            float worldTileSize,
            const TerrainFileCache& fileCache,
            const TerrainGrid& grid,
            std::vector<StreamingAction>& outActions);

        [[nodiscard]] bool isEnabled() const { return enabled; }
        void setEnabled(bool value) { enabled = value; }

    private:
        struct Candidate
        {
            TileCoord coord;
            float distSq;
        };

        StreamingConfig config;
        bool enabled = false;

        std::vector<Candidate> loadCandidates;
        std::vector<Candidate> unloadCandidates;

        [[nodiscard]] float tileDistanceSq(const TileCoord& coord, const glm::vec3& cameraPos,
                                           float worldTileSize) const;
    };
#pragma warning(pop)

} // namespace terrain

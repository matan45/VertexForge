#pragma once

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

    class TerrainWorldStreamer
    {
    public:
        explicit TerrainWorldStreamer(const StreamingConfig& config = {});

        void setConfig(const StreamingConfig& config);
        [[nodiscard]] const StreamingConfig& getConfig() const { return config; }

        std::vector<StreamingAction> update(
            const glm::vec3& cameraPos,
            float worldTileSize,
            const TerrainFileCache& fileCache,
            const TerrainGrid& grid);

        [[nodiscard]] bool isEnabled() const { return enabled; }
        void setEnabled(bool value) { enabled = value; }

    private:
        StreamingConfig config;
        bool enabled = false;

        [[nodiscard]] float tileDistanceSq(const TileCoord& coord, const glm::vec3& cameraPos,
                                           float worldTileSize) const;
    };

} // namespace terrain

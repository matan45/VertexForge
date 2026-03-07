#pragma once

#include "WaterTypes.hpp"
#include "WaterDefinitionMap.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <functional>

namespace water
{
    class WaterGrid;

    struct WaterStreamingConfig
    {
        float loadRadius = 200.0f;
        float unloadRadius = 250.0f;
        int maxLoadsPerFrame = 4;
        int maxUnloadsPerFrame = 4;
    };

    struct WaterStreamingAction
    {
        TileCoord coord;
        bool isLoad = true;
    };

    class WaterWorldStreamer
    {
    public:
        explicit WaterWorldStreamer(const WaterStreamingConfig& config = {});

        void setConfig(const WaterStreamingConfig& config);
        [[nodiscard]] const WaterStreamingConfig& getConfig() const { return config; }

        void update(
            const glm::vec3& cameraPos,
            float worldTileSize,
            const WaterDefinitionMap& definitions,
            const WaterGrid& grid,
            std::vector<WaterStreamingAction>& outActions);

        [[nodiscard]] bool isEnabled() const { return enabled; }
        void setEnabled(bool value) { enabled = value; }

    private:
        struct Candidate
        {
            TileCoord coord;
            float distSq;
        };

        WaterStreamingConfig config;
        bool enabled = false;

        std::vector<Candidate> loadCandidates;
        std::vector<Candidate> unloadCandidates;

        [[nodiscard]] float tileDistanceSq(const TileCoord& coord, const glm::vec3& cameraPos,
                                           float worldTileSize) const;
    };
}

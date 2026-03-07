#pragma once

#include "WorldTypes.hpp"
#include "WorldSectorManager.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace world
{
    struct SectorStreamingAction
    {
        SectorCoord coord;
        bool isLoad = true;
    };

    class SectorStreamer
    {
    public:
        explicit SectorStreamer(const SectorStreamingConfig& config = {});

        void setConfig(const SectorStreamingConfig& config);
        [[nodiscard]] const SectorStreamingConfig& getConfig() const { return config; }

        void update(
            const glm::vec3& cameraPos,
            const WorldSectorManager& manager,
            std::vector<SectorStreamingAction>& outActions);

        [[nodiscard]] bool isEnabled() const { return enabled; }
        void setEnabled(bool value) { enabled = value; }

    private:
        struct Candidate
        {
            SectorCoord coord;
            float distSq;
        };

        SectorStreamingConfig config;
        bool enabled = false;

        std::vector<Candidate> loadCandidates;
        std::vector<Candidate> unloadCandidates;

        [[nodiscard]] float sectorDistanceSq(const SectorCoord& coord, const glm::vec3& cameraPos,
                                              float sectorWorldSize) const;
    };

} // namespace world

#pragma once

#include "WorldTypes.hpp"
#include "WorldSectorManager.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <unordered_set>

namespace world
{
    struct SectorStreamingAction
    {
        SectorCoord coord;
        bool isLoad = true;
    };

    class SectorStreamer
    {
    private:
        struct Candidate
        {
            SectorCoord coord;
            float distSq;
        };

        SectorStreamingConfig config;
        bool enabled = false;
        bool needsSeed = true;

        std::vector<Candidate> loadCandidates;
        std::vector<Candidate> unloadCandidates;
        std::unordered_set<SectorCoord, SectorCoordHash> loadedSectors;

    public:
        explicit SectorStreamer(const SectorStreamingConfig& config = {});

        void setConfig(const SectorStreamingConfig& config);
        [[nodiscard]] const SectorStreamingConfig& getConfig() const { return config; }

        void update(
            const std::vector<StreamingSource>& sources,
            const WorldSectorManager& manager,
            std::vector<SectorStreamingAction>& outActions);

        [[nodiscard]] bool isEnabled() const { return enabled; }
        void setEnabled(bool value);

    private:
        [[nodiscard]] float sectorDistanceSq(const SectorCoord& coord, const glm::vec3& cameraPos,
                                              float sectorWorldSize) const;
        void seedLoadedSectors(const WorldSectorManager& manager);
    };

} // namespace world

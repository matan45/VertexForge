#pragma once

#include "WorldExport.hpp"
#include "WorldTypes.hpp"
#include "WorldSectorManager.hpp"
#include <glm/glm.hpp>
#include <limits>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace world
{
    struct SectorStreamingAction
    {
        SectorCoord coord;
        // VK-1591: replaces `bool isLoad`. Deliberately NOT accompanied by an isLoad() helper -
        // every consumer must decide explicitly whether a Prefetched action counts as "a load"
        // for its purposes, and removing the bool makes that a compile error rather than a silent
        // behaviour change.
        SectorTargetState target = SectorTargetState::Activated;
    };

#pragma warning(push)
#pragma warning(disable: 4251)
    class VF_WORLD_API SectorStreamer
    {
    private:
        struct Candidate
        {
            SectorCoord coord;
            float distSq;
            float sortKey; // distSq scaled by source priority for load ordering
        };

        // VK-1591: per-coord ring resolution accumulated over ALL sources before any candidate is
        // emitted. Replaces the old visitedCoords first-claim set, which let the highest-priority
        // source that merely had a coord in its SCAN BOX shadow every other source - harmless with
        // one ring, fatal with two (a priority-1 prefetch-only minimap source would silently
        // downgrade the priority-0 camera's activation ring to bytes-only).
        struct RingTarget
        {
            SectorTargetState target = SectorTargetState::Unloaded;
            float distSq = std::numeric_limits<float>::max();
            float activateKey = std::numeric_limits<float>::max();
            float prefetchKey = std::numeric_limits<float>::max();
        };

        SectorStreamingConfig config;
        bool enabled = false;
        bool needsSeed = true;

        std::vector<Candidate> activateCandidates;
        std::vector<Candidate> prefetchCandidates;
        std::vector<Candidate> unloadCandidates;
        // Renamed from loadedSectors: now also holds Prefetching/Prefetched, so the unload pass
        // can reach a prefetched sector that leaves the ring.
        std::unordered_set<SectorCoord, SectorCoordHash> trackedSectors;
        std::unordered_map<SectorCoord, RingTarget, SectorCoordHash> ringTargets;

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
        void normalizeConfig();
        void seedTrackedSectors(const WorldSectorManager& manager);
    };
#pragma warning(pop)

} // namespace world

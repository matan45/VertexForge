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

        // VK-1593: a source resolved for this frame - teleport-guarded, lookahead applied and
        // clamped, view direction projected and normalized. Built once and consumed by BOTH the
        // ring pass and the unload pass, so the two can never disagree about where a source is.
        // Sources with targetState == Unloaded are excluded entirely, preserving the "a source
        // that wants nothing contributes nothing" skips this replaces.
        struct SourceEval
        {
            glm::vec3 position{0.0f};
            glm::vec3 predicted{0.0f};  // == position when lookahead is off or was suppressed
            glm::vec2 viewDirXZ{0.0f};  // normalized; zero == omni
            float radiusMultiplier = 1.0f;
            uint8_t priority = 0;
            SectorTargetState targetState = SectorTargetState::Activated;
        };

        SectorStreamingConfig config;
        bool enabled = false;
        bool needsSeed = true;

        std::vector<Candidate> activateCandidates;
        std::vector<Candidate> prefetchCandidates;
        std::vector<Candidate> unloadCandidates;
        std::vector<SourceEval> sourceEvals;
        // Renamed from loadedSectors: now also holds Prefetching/Prefetched, so the unload pass
        // can reach a prefetched sector that leaves the ring.
        std::unordered_set<SectorCoord, SectorCoordHash> trackedSectors;
        std::unordered_map<SectorCoord, RingTarget, SectorCoordHash> ringTargets;

        // VK-1593: last frame's position per source id, for teleport detection. Rebuilt into the
        // scratch map every frame and swapped, so a source that disappears drops out without a
        // mark-and-sweep. Only touched when a feature that depends on it is enabled.
        std::unordered_map<uint32_t, glm::vec3> lastSourcePositions;
        std::unordered_map<uint32_t, glm::vec3> sourcePositionScratch;
        int burstFramesRemaining = 0;
        // True for exactly the frames on which the burst budget was applied. Separate from the
        // counter because the counter is decremented inside update(), and the service reads the
        // burst state AFTER update() returns to pick its entity budget - with burstFrames == 1
        // the counter would already be back to 0 by then.
        bool burstActive = false;

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

        // VK-1593: forget every source's motion history and close any burst window. Call this when
        // the SOURCE SET itself is torn down - the service pairs it with clearStreamingSources(),
        // which resets nextStreamingSourceId to 1, so without it a freshly registered source would
        // inherit the last position of the id-1 source from the previous world and read as a jump.
        //
        // Deliberately NOT hung off setEnabled(): that is called MID-FRAME from the edit-mode
        // selected-entity unload rail (WorldSectorStreamingOps.cpp), after update() has already
        // recorded this frame's positions. Resetting there silently disables teleport detection
        // and truncates the burst for as long as a selected entity sits outside the ring.
        void resetMotionTracking();

        // VK-1593: true on the frames the camera-jump burst budget was applied by the most
        // recent update(). The service reads this to widen its own per-frame entity budget.
        [[nodiscard]] bool isBursting() const { return burstActive; }
        // Frames of burst window left AFTER the most recent update() - a UI/telemetry readout.
        [[nodiscard]] int getBurstFramesRemaining() const { return burstFramesRemaining; }

    private:
        [[nodiscard]] float sectorDistanceSq(const SectorCoord& coord, const glm::vec3& cameraPos,
                                              float sectorWorldSize) const;
        // VK-1593: the metric every ring decision uses - the smaller of the distance to where the
        // source is and to where it is predicted to be.
        [[nodiscard]] float sourceDistanceSq(const SourceEval& eval, const SectorCoord& coord,
                                             float sectorWorldSize) const;
        void buildSourceEvals(const std::vector<StreamingSource>& sources, float sectorWorldSize);
        void normalizeConfig();
        void seedTrackedSectors(const WorldSectorManager& manager);
    };
#pragma warning(pop)

} // namespace world

#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace animation
{
    enum class AnimationLODLevel : uint8_t
    {
        LOD0 = 0,  // 0-25m: every frame, full quality
        LOD1 = 1,  // 25-75m: every 2nd frame, interpolate cached poses
        LOD2 = 2,  // 75-150m: every 4th-8th frame, simplified evaluation
        LOD3 = 3,  // 150m+: frozen pose, zero CPU cost
        Count = 4
    };

    struct AnimationLODConfig
    {
        float distanceThresholds[4] = {25.0f, 75.0f, 150.0f, 300.0f};
        uint32_t updateIntervals[4] = {1, 2, 6, 0};  // 0 = frozen
    };

    struct EntityAnimationLODState
    {
        AnimationLODLevel currentLOD = AnimationLODLevel::LOD0;
        uint32_t framesSinceLastEval = 0;
        float distanceSquared = 0.0f;

        std::vector<glm::mat4> cachedPoseA;
        std::vector<glm::mat4> cachedPoseB;
        bool hasCachedPoseA = false;
        bool hasCachedPoseB = false;
    };

    class AnimationLODManager
    {
    public:
        AnimationLODManager() = default;

        void setConfig(const AnimationLODConfig& cfg) { config = cfg; }
        const AnimationLODConfig& getConfig() const { return config; }
        AnimationLODConfig& getMutableConfig() { return config; }

        AnimationLODLevel computeLODLevel(float distanceSquared) const;
        bool shouldEvaluateThisFrame(const EntityAnimationLODState& state) const;

        void updateEntityLOD(EntityAnimationLODState& state, float distanceSquared);

        void interpolateCachedPoses(const EntityAnimationLODState& state,
                                     float t,
                                     std::vector<glm::mat4>& outMatrices) const;

        void cachePose(EntityAnimationLODState& state, const std::vector<glm::mat4>& pose);

        uint32_t getLODCount(AnimationLODLevel level) const { return lodCounts[static_cast<uint8_t>(level)]; }
        void resetLODCounts() { for (auto& c : lodCounts) c = 0; }
        void incrementLODCount(AnimationLODLevel level) { ++lodCounts[static_cast<uint8_t>(level)]; }

    private:
        AnimationLODConfig config;
        uint32_t lodCounts[4] = {0, 0, 0, 0};
    };
}

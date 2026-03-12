#pragma once

#include "GPULightTypes.hpp"
#include "../gpudriven/FreeListAllocator.hpp"
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>

namespace render::lighting
{
    struct LightStreamEntry
    {
        uint32_t entityId = 0;
        uint32_t slotIndex = 0;     // Index in the GPU buffer (from free-list)
        uint32_t sectorId = 0;
        float priority = 0.0f;
        float distance = 0.0f;
        float intensity = 0.0f;
        float radius = 0.0f;
        bool castsShadow = false;
        bool isStatic = false;      // Static lights get priority bonus (cheaper to keep active)
        bool active = true;         // Whether currently uploaded to GPU

        enum class LightType : uint8_t { Point, Spot } type = LightType::Point;
    };

    struct LightStreamingConfig
    {
        uint32_t maxPointLights = 1024;
        uint32_t maxSpotLights = 512;

        // Priority weights for scoring
        float distanceWeight = 1.0f;
        float intensityWeight = 0.5f;
        float radiusWeight = 0.3f;
        float shadowWeight = 2.0f;

        // Static light optimization: bonus priority for static lights (cached shadows are cheaper)
        float staticBonus = 0.3f;
        float staticHysteresisMultiplier = 2.0f;

        // Hysteresis: lights within this margin of the cutoff priority won't pop in/out
        float hysteresisMargin = 0.05f;
    };

    struct LightStreamingStats
    {
        uint32_t registeredPointLights = 0;
        uint32_t registeredSpotLights = 0;
        uint32_t activePointLights = 0;
        uint32_t activeSpotLights = 0;
        uint32_t excludedByBudget = 0;
        float pointPoolUtilization = 0.0f;
        float spotPoolUtilization = 0.0f;
        float pointFragmentation = 0.0f;
        float spotFragmentation = 0.0f;
    };

    class LightStreamManager
    {
    private:
        gpudriven::FreeListAllocator pointAllocator;
        gpudriven::FreeListAllocator spotAllocator;

        std::unordered_map<uint32_t, LightStreamEntry> registeredLights;
        std::unordered_set<uint32_t> activeLightIds;

        // Sector tracking: sectorId -> set of entityIds
        std::unordered_map<uint32_t, std::unordered_set<uint32_t>> sectorLights;

        LightStreamingConfig config;

        // Priority scoring state
        glm::vec3 cameraPosition{0.0f};
        std::vector<std::pair<uint32_t, float>> sortedPriorities; // entityId, priority

        mutable std::mutex mtx;

    public:
        LightStreamManager();
        ~LightStreamManager() = default;

        LightStreamManager(const LightStreamManager&) = delete;
        LightStreamManager& operator=(const LightStreamManager&) = delete;

        void init(const LightStreamingConfig& cfg = {});
        void cleanup();

        // Sector lifecycle
        void registerSectorLights(uint32_t sectorId, const std::vector<uint32_t>& lightEntityIds);
        void unregisterSectorLights(uint32_t sectorId);

        // Individual light management
        bool registerLight(uint32_t entityId, LightStreamEntry::LightType type,
                          uint32_t sectorId = 0);
        void unregisterLight(uint32_t entityId);

        // Priority update
        void updatePriorities(const glm::vec3& cameraPos);
        void applyBudget();

        // Access
        bool isLightActive(uint32_t entityId) const;
        const std::unordered_set<uint32_t>& getActiveLightIds() const { return activeLightIds; }
        uint32_t getSlotIndex(uint32_t entityId) const;

        // Configuration
        void setConfig(const LightStreamingConfig& cfg);
        const LightStreamingConfig& getConfig() const { return config; }

        // Stats
        LightStreamingStats getStats() const;

    private:
        float computePriority(const LightStreamEntry& entry) const;
        bool allocateSlot(LightStreamEntry& entry);
        void freeSlot(LightStreamEntry& entry);
    };
}

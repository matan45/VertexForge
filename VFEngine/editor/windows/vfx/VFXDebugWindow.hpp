#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace windows
{
    // VK-1453 (Phase 4) — VFX debug window. Tabs: Budget (emitter/particle/LOD/pool),
    // Combos (active sequence combos + child/culled/pooled counts), Instances
    // (per-instance bounds/cull table), Warnings (deduplicated runtime warnings).
    // All data pulled via CQRS on a shared refresh timer; queries are individually
    // guarded so a missing handler (e.g. no runtime) just leaves that tab empty.
    class VFXDebugWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        bool visible = false;
        float refreshTimer = 0.0f;
        static constexpr float REFRESH_INTERVAL = 0.25f;

        struct BudgetStats
        {
            uint32_t activeEmitters = 0;
            uint32_t maxEmitters = 0;
            uint32_t allocatedParticles = 0;
            uint32_t maxParticles = 0;
            uint32_t lodCounts[4] = {0, 0, 0, 0};
            float fragmentationPercent = 0.0f;
            uint32_t poolWarmSlots = 0;
            uint32_t poolUsedSlots = 0;
            uint32_t poolTotalSlots = 0;
            uint32_t culledEmitters = 0;
            uint32_t throttledEmitters = 0;
            float vfxCullDistance = 0.0f;
            uint32_t eventsThisFrame = 0;
            uint32_t rawEventsThisFrame = 0;
            uint32_t eventBudget = 0;
            bool eventsDropped = false;
            uint32_t channelListeners = 0;
            uint32_t channelRawRequests = 0;
            uint32_t channelAcceptedRequests = 0;
            uint32_t channelRingDroppedRequests = 0;
            uint32_t channelParticleDroppedRequests = 0;
            uint32_t channelRequestBudget = 0;
            // VK-1503 (M4 slice-c)
            uint32_t evictedInstances = 0;
            uint32_t maxLiveInstances = 0;
        };

        struct ComboStats
        {
            uint32_t activeCombos = 0;
            uint32_t playingCombos = 0;
            uint32_t liveChildInstances = 0;
            uint32_t culledSpawns = 0;
            uint32_t pooledReuses = 0;
        };

        struct InstanceEntry
        {
            uint32_t id = 0;
            float worldPos[3] = {0, 0, 0};
            float extents[3] = {0, 0, 0};
            bool inFrustum = true;
            uint8_t lod = 0;
            uint32_t particleCount = 0;
            uint8_t priority = 2;
        };

        struct WarningEntry
        {
            std::string source;
            std::string message;
            uint32_t count = 0;
            uint64_t lastSeq = 0;
        };

        BudgetStats budget;
        ComboStats combos;
        std::vector<InstanceEntry> instances;
        std::vector<WarningEntry> warnings;

        // VK-1503 (M4 slice-c) — editable significance-cap budget lever (0 = unlimited).
        int significanceBudgetInput = 0;

    public:
        VFXDebugWindow() = default;
        ~VFXDebugWindow() override = default;

        void draw() override;
        void show() { visible = true; }

    private:
        void refreshData();
        void drawBudgetTab();
        void drawCombosTab();
        void drawInstancesTab();
        void drawWarningsTab();
    };
}

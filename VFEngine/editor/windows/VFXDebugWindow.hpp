#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include <cstdint>

namespace windows
{
    class VFXDebugWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        bool visible = false;
        float refreshTimer = 0.0f;
        static constexpr float REFRESH_INTERVAL = 0.25f;

        struct CachedStats
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
        };

        CachedStats stats;

    public:
        VFXDebugWindow() = default;
        ~VFXDebugWindow() override = default;

        void draw() override;
        void show() { visible = true; }

    private:
        void refreshData();
    };
}

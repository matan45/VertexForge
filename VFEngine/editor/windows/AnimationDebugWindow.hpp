#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include <cstdint>

namespace windows
{
    class AnimationDebugWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        bool visible = false;
        float refreshTimer = 0.0f;
        static constexpr float REFRESH_INTERVAL = 0.25f;

        struct CachedStats
        {
            uint32_t totalAnimators = 0;
            uint32_t culledEntities = 0;
            uint32_t lodCounts[4] = {0, 0, 0, 0};
            uint32_t pendingStreamingInits = 0;
        };

        CachedStats stats;

        // LOD config (ST-10)
        float lodDistances[4] = {25.0f, 75.0f, 150.0f, 300.0f};
        int lodUpdateIntervals[4] = {1, 2, 6, 0};
        int maxStreamingInitPerFrame = 4;

    public:
        explicit AnimationDebugWindow() = default;
        ~AnimationDebugWindow() override = default;

        void draw() override;
        void show() { visible = true; }

    private:
        void refreshData();
        void drawConfigSection();
    };
}

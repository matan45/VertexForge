#pragma once
#include "events/EventDispatcher.hpp"
#include "config/EditorPreferences.hpp"
#include <cstdint>
#include <string>

struct ImGuiViewport;

namespace windows
{
    class StatusBar
    {
    private:
        float height = 24.0f;
        float refreshTimer = 0.0f;
        static constexpr float refreshInterval = 0.25f;

        uint32_t cachedDrawCalls = 0;
        uint64_t cachedVramMB = 0;
        std::string currentSceneName;

        float smoothedViewportFps = 0.0f;          // 0 = uninitialized
        static constexpr float fpsEmaAlpha = 0.1f; // ~90% response in ~22 frames @60fps

        // VSync (Fifo present mode) caps the presented frame to the refresh interval;
        // when on, the viewport frame time is floored so the FPS matches the Runtime cap.
        bool vsyncEnabled = false;
        uint32_t refreshHz = 0;

        config::DebugSettings debugSettings;

        events::SubscriptionToken sceneLoadedToken;
        events::SubscriptionToken sceneClearedToken;
        events::SubscriptionToken settingsChangedToken;
        events::SubscriptionToken displaySettingsToken;

    public:
        StatusBar();
        ~StatusBar();

        void draw(const ImGuiViewport* viewport);
        float getHeight() const { return height; }
    };
}

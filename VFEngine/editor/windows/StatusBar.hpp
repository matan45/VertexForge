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

        config::DebugSettings debugSettings;

        events::SubscriptionToken sceneLoadedToken;
        events::SubscriptionToken sceneClearedToken;
        events::SubscriptionToken settingsChangedToken;

    public:
        StatusBar();
        ~StatusBar();

        void draw(const ImGuiViewport* viewport);
        float getHeight() const { return height; }
    };
}

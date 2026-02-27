#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/LightBakeEvents.hpp"
#include <string>
#include <memory>
#include <mutex>
#include <atomic>

namespace windows
{
    class LightBakeWindow : public controllers::imguiHandler::ImguiWindow
    {
    public:
        LightBakeWindow();
        ~LightBakeWindow() override;

        void draw() override;
        void show();

    private:
        void drawSettings();
        void drawBakeActions();
        void drawProgress();
        void drawResult();

        bool visible = false;
        std::atomic<bool> baking{false};
        std::atomic<float> bakeProgress{0.0f};

        // Settings
        float texelsPerUnit = 16.0f;
        int maxAtlasSize = 4096;

        // Result
        services::LightBakeResult lastResult;
        bool hasResult = false;
        mutable std::mutex resultMutex;

        events::SubscriptionToken bakeCompleteToken;
        events::SubscriptionToken bakeFailedToken;
    };
}

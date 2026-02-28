#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/LightBakeEvents.hpp"
#include "nfd/FileDialog.hpp"
#include <string>
#include <mutex>
#include <atomic>

namespace windows
{
    class LightBakeWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        bool visible = false;
        std::atomic<bool> baking{false};
        std::atomic<float> bakeProgress{0.0f};

        float texelsPerUnit = 16.0f;
        int maxAtlasSize = 4096;
        std::string outputPath;

        services::LightBakeResult lastResult;
        bool hasResult = false;
        mutable std::mutex resultMutex;

        nfd::FileDialog fileDialog;

        events::SubscriptionToken bakeCompleteToken;
        events::SubscriptionToken bakeFailedToken;
        events::SubscriptionToken bakeCancelledToken;

    public:
        explicit LightBakeWindow();
        ~LightBakeWindow() override;

        void draw() override;
        void show();

    private:
        void drawSettings();
        void drawBakeActions();
        void drawProgress();
        void drawResult();
        void drawSaveToScene();
    };
}

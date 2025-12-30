#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include <string>
#include <mutex>
#include <atomic>

namespace windows
{
    class SceneLoadingProgressWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        std::atomic<bool> showWindow{false};
        std::atomic<float> currentProgress{0.0f};
        std::string currentEntityName;
        std::string scenePath;
        std::atomic<bool> loadingComplete{false};
        std::atomic<bool> loadingSuccess{false};
        std::string errorMessage;
        mutable std::mutex dataMutex;

        events::SubscriptionToken startToken;
        events::SubscriptionToken progressToken;
        events::SubscriptionToken completeToken;
    public:
        explicit SceneLoadingProgressWindow();
        ~SceneLoadingProgressWindow() override;

        void draw() override;
    };
}

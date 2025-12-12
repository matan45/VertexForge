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
    public:
        SceneLoadingProgressWindow();
        ~SceneLoadingProgressWindow() override;

        void draw() override;

    private:
        std::atomic<bool> showWindow{false};
        std::atomic<float> currentProgress{0.0f};
        std::atomic<size_t> entitiesLoaded{0};
        std::atomic<size_t> totalEntities{0};
        std::string currentEntityName;
        std::string scenePath;
        std::atomic<bool> loadingComplete{false};
        std::atomic<bool> loadingSuccess{false};
        std::string errorMessage;
        mutable std::mutex dataMutex;

        events::SubscriptionToken startToken;
        events::SubscriptionToken progressToken;
        events::SubscriptionToken completeToken;
    };
}

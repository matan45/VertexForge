#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include <string>
#include <mutex>
#include <atomic>

namespace windows
{
    class SceneLoadProgressWindow : public controllers::imguiHandler::ImguiWindow
    {
    public:
        SceneLoadProgressWindow();
        ~SceneLoadProgressWindow() override;

        void draw() override;

    private:
        std::atomic<bool> showWindow{false};
        std::atomic<float> currentProgress{0.0f};
        std::string currentStatus;
        mutable std::mutex statusMutex;

        events::SubscriptionToken startToken;
        events::SubscriptionToken progressToken;
        events::SubscriptionToken completeToken;
    };
}

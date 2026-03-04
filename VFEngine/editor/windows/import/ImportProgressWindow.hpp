#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/ResourceEvents.hpp"
#include <string>
#include <mutex>
#include <atomic>

namespace windows
{
    class ImportProgressWindow : public controllers::imguiHandler::ImguiWindow
    {
    public:
        ImportProgressWindow();
        ~ImportProgressWindow() override;

        void draw() override;

    private:
        std::atomic<bool> showWindow{false};
        std::atomic<float> currentProgress{0.0f};
        std::string currentFile;
        mutable std::mutex fileMutex;

        events::SubscriptionToken startToken;
        events::SubscriptionToken progressToken;
        events::SubscriptionToken completeToken;
    };
}

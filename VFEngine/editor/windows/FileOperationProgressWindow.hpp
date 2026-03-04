#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/FileOperationsEvents.hpp"
#include <string>
#include <mutex>
#include <atomic>
#include <vector>

namespace windows
{
    class FileOperationProgressWindow : public controllers::imguiHandler::ImguiWindow
    {
    public:
        FileOperationProgressWindow();
        ~FileOperationProgressWindow() override;

        void draw() override;

    private:
        std::atomic<bool> showWindow{false};
        std::atomic<float> currentProgress{0.0f};
        std::atomic<bool> finished{false};
        std::atomic<bool> success{true};

        std::string operationType;
        std::string currentFile;
        std::string errorMessage;
        std::vector<std::string> conflicts;
        uint32_t completedCount = 0;
        uint32_t totalCount = 0;
        mutable std::mutex dataMutex;

        events::SubscriptionToken startToken;
        events::SubscriptionToken progressToken;
        events::SubscriptionToken completeToken;
    };
}

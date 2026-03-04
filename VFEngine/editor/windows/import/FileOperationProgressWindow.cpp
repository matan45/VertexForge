#include "FileOperationProgressWindow.hpp"
#include "imgui.h"

namespace windows
{
    FileOperationProgressWindow::FileOperationProgressWindow()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        startToken = dispatcher.subscribe<events::fileops::FileOpBatchStartedNotification>(
            [this](const events::fileops::FileOpBatchStartedNotification& notif)
            {
                showWindow.store(true);
                currentProgress.store(0.0f);
                finished.store(false);
                success.store(true);
                {
                    std::lock_guard<std::mutex> lock(dataMutex);
                    operationType = notif.operationType;
                    currentFile = "Starting...";
                    errorMessage.clear();
                    conflicts.clear();
                    completedCount = 0;
                    totalCount = notif.totalOperations;
                }
            });

        progressToken = dispatcher.subscribe<events::fileops::FileOpBatchProgressNotification>(
            [this](const events::fileops::FileOpBatchProgressNotification& notif)
            {
                float progress = notif.total > 0
                    ? static_cast<float>(notif.completed) / static_cast<float>(notif.total)
                    : 0.0f;
                currentProgress.store(progress);
                {
                    std::lock_guard<std::mutex> lock(dataMutex);
                    currentFile = notif.currentFile;
                    completedCount = notif.completed;
                    totalCount = notif.total;
                }
            });

        completeToken = dispatcher.subscribe<events::fileops::FileOpBatchCompletedNotification>(
            [this](const events::fileops::FileOpBatchCompletedNotification& notif)
            {
                currentProgress.store(1.0f);
                finished.store(true);
                success.store(notif.success);
                {
                    std::lock_guard<std::mutex> lock(dataMutex);
                    currentFile = notif.success ? "Complete!" : "Failed";
                    errorMessage = notif.errorMessage;
                    conflicts = notif.conflicts;
                }
            });
    }

    FileOperationProgressWindow::~FileOperationProgressWindow()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        dispatcher.unsubscribe(startToken);
        dispatcher.unsubscribe(progressToken);
        dispatcher.unsubscribe(completeToken);
    }

    void FileOperationProgressWindow::draw()
    {
        if (!showWindow.load()) return;

        bool windowOpen = true;
        float progress = currentProgress.load();
        bool isFinished = finished.load();
        bool isSuccess = success.load();

        ImGui::SetNextWindowSize(ImVec2(420, 170), ImGuiCond_Always);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize;

        std::string opType;
        std::string fileName;
        uint32_t completed = 0;
        uint32_t total = 0;
        std::string error;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            opType = operationType;
            fileName = currentFile;
            completed = completedCount;
            total = totalCount;
            error = errorMessage;
        }

        std::string title = opType + "...";
        if (ImGui::Begin(title.c_str(), &windowOpen, flags))
        {
            if (total > 1)
            {
                ImGui::Text("%s (%u / %u): %s", opType.c_str(), completed, total, fileName.c_str());
            }
            else
            {
                ImGui::Text("%s: %s", opType.c_str(), fileName.c_str());
            }

            ImGui::Spacing();
            ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f));
            ImGui::Spacing();

            if (isFinished)
            {
                ImGui::Separator();

                if (!isSuccess)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Errors occurred:");
                    ImGui::TextWrapped("%s", error.c_str());
                    ImGui::Spacing();
                }

                if (ImGui::Button("Close", ImVec2(-1.0f, 0.0f)))
                {
                    windowOpen = false;
                }
            }
        }
        ImGui::End();

        if (!windowOpen)
        {
            showWindow.store(false);
        }
    }
}

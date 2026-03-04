#include "ImportProgressWindow.hpp"
#include "imgui.h"

namespace windows {

    ImportProgressWindow::ImportProgressWindow()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        startToken = dispatcher.subscribe<events::resource::ImportStartedNotification>(
            [this](const events::resource::ImportStartedNotification&) {
                showWindow.store(true);
                currentProgress.store(0.0f);
                {
                    std::lock_guard<std::mutex> lock(fileMutex);
                    currentFile = "Starting...";
                }
            });

        progressToken = dispatcher.subscribe<events::resource::ImportProgressNotification>(
            [this](const events::resource::ImportProgressNotification& notif) {
                currentProgress.store(notif.progress);
                {
                    std::lock_guard<std::mutex> lock(fileMutex);
                    currentFile = notif.currentFile;
                }
            });

        completeToken = dispatcher.subscribe<events::resource::ImportCompletedNotification>(
            [this](const events::resource::ImportCompletedNotification&) {
                currentProgress.store(1.0f);
                {
                    std::lock_guard<std::mutex> lock(fileMutex);
                    currentFile = "Complete!";
                }
            });
    }

    ImportProgressWindow::~ImportProgressWindow()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        dispatcher.unsubscribe(startToken);
        dispatcher.unsubscribe(progressToken);
        dispatcher.unsubscribe(completeToken);
    }

    void ImportProgressWindow::draw()
    {
        if (!showWindow.load()) return;

        bool windowOpen = true;
        float progress = currentProgress.load();

        // Use fixed window size to prevent resize when Close button appears
        ImGui::SetNextWindowSize(ImVec2(400, 150), ImGuiCond_Always);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize;
        if (ImGui::Begin("Importing...", &windowOpen, flags)) {
            // Get current file name thread-safely
            std::string fileName;
            {
                std::lock_guard<std::mutex> lock(fileMutex);
                fileName = currentFile;
            }
            
            ImGui::Text("Current: %s", fileName.c_str());
            ImGui::Spacing();
            
            ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f));

            ImGui::Spacing();
            
            if (progress >= 1.0f) {
                ImGui::Separator();
                if (ImGui::Button("Close", ImVec2(-1.0f, 0.0f))) {
                    windowOpen = false;
                }
            }
        }
        ImGui::End();

        // Update window visibility if closed
        if (!windowOpen) {
            showWindow.store(false);
        }
    }
}

#include "SceneLoadProgressWindow.hpp"
#include "imgui.h"

namespace windows {

    SceneLoadProgressWindow::SceneLoadProgressWindow()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        startToken = dispatcher.subscribe<events::scene::SceneLoadingStartedNotification>(
            [this](const events::scene::SceneLoadingStartedNotification&) {
                showWindow.store(true);
                currentProgress.store(0.0f);
                {
                    std::lock_guard<std::mutex> lock(statusMutex);
                    currentStatus = "Starting...";
                }
            });

        progressToken = dispatcher.subscribe<events::scene::SceneLoadingProgressUpdatedNotification>(
            [this](const events::scene::SceneLoadingProgressUpdatedNotification& notif) {
                currentProgress.store(notif.progress);
                {
                    std::lock_guard<std::mutex> lock(statusMutex);
                    currentStatus = notif.currentEntityName;
                }
            });

        completeToken = dispatcher.subscribe<events::scene::SceneLoadingCompletedNotification>(
            [this](const events::scene::SceneLoadingCompletedNotification& notif) {
                currentProgress.store(1.0f);
                {
                    std::lock_guard<std::mutex> lock(statusMutex);
                    currentStatus = notif.success ? "Complete!" : notif.errorMessage;
                }
            });
    }

    SceneLoadProgressWindow::~SceneLoadProgressWindow()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        dispatcher.unsubscribe(startToken);
        dispatcher.unsubscribe(progressToken);
        dispatcher.unsubscribe(completeToken);
    }

    void SceneLoadProgressWindow::draw()
    {
        if (!showWindow.load()) return;

        bool windowOpen = true;
        float progress = currentProgress.load();

        ImGui::SetNextWindowSize(ImVec2(400, 150), ImGuiCond_Always);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize;
        if (ImGui::Begin("Loading Scene...", &windowOpen, flags)) {
            std::string status;
            {
                std::lock_guard<std::mutex> lock(statusMutex);
                status = currentStatus;
            }

            ImGui::Text("Current: %s", status.c_str());
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

        if (!windowOpen) {
            showWindow.store(false);
        }
    }
}

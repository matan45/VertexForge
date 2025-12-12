#include "SceneLoadingProgressWindow.hpp"
#include "imgui.h"

namespace windows {

    SceneLoadingProgressWindow::SceneLoadingProgressWindow()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        startToken = dispatcher.subscribe<events::scene::SceneLoadingStartedNotification>(
            [this](const events::scene::SceneLoadingStartedNotification& notif) {
                showWindow.store(true);
                currentProgress.store(0.0f);
                entitiesLoaded.store(0);
                totalEntities.store(notif.totalEntities);
                loadingComplete.store(false);
                loadingSuccess.store(false);
                {
                    std::lock_guard<std::mutex> lock(dataMutex);
                    currentEntityName = "Starting...";
                    scenePath = notif.scenePath;
                    errorMessage.clear();
                }
            });

        progressToken = dispatcher.subscribe<events::scene::SceneLoadingProgressNotification>(
            [this](const events::scene::SceneLoadingProgressNotification& notif) {
                currentProgress.store(notif.progress);
                entitiesLoaded.store(notif.entitiesLoaded);
                totalEntities.store(notif.totalEntities);
                {
                    std::lock_guard<std::mutex> lock(dataMutex);
                    currentEntityName = notif.currentEntityName;
                }
            });

        completeToken = dispatcher.subscribe<events::scene::SceneLoadingCompletedNotification>(
            [this](const events::scene::SceneLoadingCompletedNotification& notif) {
                currentProgress.store(1.0f);
                loadingComplete.store(true);
                loadingSuccess.store(notif.success);
                {
                    std::lock_guard<std::mutex> lock(dataMutex);
                    if (notif.success) {
                        currentEntityName = "Complete!";
                    } else {
                        currentEntityName = "Failed!";
                        errorMessage = notif.errorMessage;
                    }
                }
            });
    }

    SceneLoadingProgressWindow::~SceneLoadingProgressWindow()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        dispatcher.unsubscribe(startToken);
        dispatcher.unsubscribe(progressToken);
        dispatcher.unsubscribe(completeToken);
    }

    void SceneLoadingProgressWindow::draw()
    {
        if (!showWindow.load()) return;

        bool windowOpen = true;
        float progress = currentProgress.load();
        bool complete = loadingComplete.load();
        bool success = loadingSuccess.load();
        size_t loaded = entitiesLoaded.load();
        size_t total = totalEntities.load();

        ImGui::SetNextWindowSize(ImVec2(450, 180), ImGuiCond_Always);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize;
        if (ImGui::Begin("Loading Scene...", &windowOpen, flags)) {
            // Get data thread-safely
            std::string entityName;
            std::string path;
            std::string error;
            {
                std::lock_guard<std::mutex> lock(dataMutex);
                entityName = currentEntityName;
                path = scenePath;
                error = errorMessage;
            }

            // Scene path
            ImGui::TextDisabled("File: %s", path.c_str());
            ImGui::Spacing();

            // Current entity being loaded
            ImGui::Text("Loading: %s", entityName.c_str());

            // Entity count
            if (total > 0) {
                ImGui::Text("Entities: %zu / %zu", loaded, total);
            }

            ImGui::Spacing();

            // Progress bar
            ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f));

            ImGui::Spacing();

            // Show error message if failed
            if (complete && !success && !error.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                ImGui::TextWrapped("Error: %s", error.c_str());
                ImGui::PopStyleColor();
            }

            // Close button when complete
            if (complete) {
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

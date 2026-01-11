#include "MainImguiWindow.hpp"
#include "events/SceneEvents.hpp"
#include "events/ApplicationEvents.hpp"
#include <imgui.h>

namespace windows
{
    MainImguiWindow::MainImguiWindow()
    {
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoBackground;
        window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove;
        window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        windowFlags = window_flags;

        // Wire up menu bar to sub-windows
        menuBar.setWindows(&iblWindow, &editorCameraWindow, &cullingStatsWindow, &importDialog, &physicsConfigWindow);

        subscribeToEvents();
    }

    MainImguiWindow::~MainImguiWindow()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        dispatcher.unsubscribe(sceneClearedToken);
        dispatcher.unsubscribe(openImportDialogToken);
    }

    void MainImguiWindow::subscribeToEvents()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        sceneClearedToken = dispatcher.subscribe<events::scene::SceneClearedNotification>(
            [this](const events::scene::SceneClearedNotification&)
            {
                onSceneCleared();
            });

        openImportDialogToken = dispatcher.subscribe<events::application::OpenImportDialogNotification>(
            [this](const events::application::OpenImportDialogNotification&)
            {
                importDialog.openImportDialog();
            });
    }

    void MainImguiWindow::onSceneCleared()
    {
        iblWindow.onSceneCleared();
    }

    void MainImguiWindow::draw()
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (ImGui::Begin("Vulkan Engine", nullptr, windowFlags))
        {
            ImGui::PopStyleVar(1);

            ImGui::DockSpace(ImGui::GetID("MyDockSpace"), ImVec2(0.0f, 0.0f),
                             ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_None);

            menuBar.draw();
            iblWindow.draw();
            editorCameraWindow.draw();
            cullingStatsWindow.draw();
            physicsConfigWindow.draw();
        }
        ImGui::End();
    }
}

#include "MainImguiWindow.hpp"
#include "events/SceneEvents.hpp"
#include "events/ApplicationEvents.hpp"
#include "events/TerrainEvents.hpp"
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

        menuBar.setWindows(&iblWindow, &editorCameraWindow, &cullingStatsWindow, &importDialog, &physicsConfigWindow, &audioConfigWindow, &renderConfigWindow);
        menuBar.setProjectSettingsWindow(&projectSettingsWindow);
        menuBar.setTerrainCreationWindow(&terrainCreationWindow);
        menuBar.setPostProcessConfigWindow(&postProcessConfigWindow);
        menuBar.setWaterEditorWindow(&waterEditorWindow);
        menuBar.setNavmeshWindow(&navmeshWindow);

        subscribeToEvents();
    }

    MainImguiWindow::~MainImguiWindow()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        dispatcher.unsubscribe(sceneClearedToken);
        dispatcher.unsubscribe(sceneLoadedToken);
        dispatcher.unsubscribe(openImportDialogToken);
        dispatcher.unsubscribe(terrainLoadStartedToken);
    }

    void MainImguiWindow::subscribeToEvents()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        sceneClearedToken = dispatcher.subscribe<events::scene::SceneClearedNotification>(
            [this](const events::scene::SceneClearedNotification&)
            {
                onSceneCleared();
            });

        sceneLoadedToken = dispatcher.subscribe<events::scene::SceneLoadedNotification>(
            [this](const events::scene::SceneLoadedNotification&)
            {
                postProcessConfigWindow.notifySceneLoaded();
            });

        openImportDialogToken = dispatcher.subscribe<events::application::OpenImportDialogNotification>(
            [this](const events::application::OpenImportDialogNotification&)
            {
                importDialog.openImportDialog();
            });

        terrainLoadStartedToken = dispatcher.subscribe<events::terrain::TerrainLoadStartedNotification>(
            [this](const events::terrain::TerrainLoadStartedNotification&)
            {
                isLoadingTerrain = true;
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
            audioConfigWindow.draw();
            renderConfigWindow.draw();
            postProcessConfigWindow.draw();
            projectSettingsWindow.draw();
            terrainCreationWindow.draw();
            waterEditorWindow.draw();
            sculptToolPanel.draw();
            paintToolPanel.draw();

            pollTerrainLoad();
        }
        ImGui::End();
    }

    void MainImguiWindow::pollTerrainLoad()
    {
        if (!isLoadingTerrain)
            return;

        // Show loading indicator
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() * 0.5f - 60.0f, ImGui::GetWindowHeight() * 0.5f));
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Loading terrain...");

        auto& dispatcher = events::EventDispatcher::instance();
        events::terrain::PollTerrainLoadCommand pollCmd;
        auto result = dispatcher.execute(pollCmd);

        if (!result.has_value())
            return;

        isLoadingTerrain = false;

        auto handle = result.value();
        if (handle.id != 0)
        {
            events::scene::SelectEntityCommand selectCmd;
            selectCmd.entity = handle;
            dispatcher.execute(selectCmd);
        }
    }
}

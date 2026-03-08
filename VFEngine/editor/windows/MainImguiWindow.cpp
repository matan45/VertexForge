#include "MainImguiWindow.hpp"
#include "events/project/SceneEvents.hpp"
#include "events/project/ApplicationEvents.hpp"
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
        menuBar.setLightBakeWindow(&lightBakeWindow);
        menuBar.setAssetLifecycleWindow(&assetLifecycleWindow);
        menuBar.setWorldSectorWindow(&worldSectorWindow);
        menuBar.setVFXDebugWindow(&vfxDebugWindow);
        menuBar.setAnimationDebugWindow(&animationDebugWindow);
        subscribeToEvents();
    }

    MainImguiWindow::~MainImguiWindow()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        dispatcher.unsubscribe(sceneClearedToken);
        dispatcher.unsubscribe(sceneLoadedToken);
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
            navmeshWindow.draw();
            lightBakeWindow.draw();
            assetLifecycleWindow.draw();
            worldSectorWindow.draw();
            vfxDebugWindow.draw();
            animationDebugWindow.draw();
            sculptToolPanel.draw();
            paintToolPanel.draw();
            holeToolPanel.draw();
            grassDensityPanel.draw();
            vegetationPlacementPanel.draw();
            vegetationSpeciesPanel.draw();
        }
        ImGui::End();
    }
}

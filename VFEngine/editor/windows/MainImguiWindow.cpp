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
        menuBar.setAudioMixerWindow(&audioMixerWindow);
        menuBar.setProjectSettingsWindow(&projectSettingsWindow);
        menuBar.setTerrainCreationWindow(&terrainCreationWindow);
        menuBar.setPostProcessConfigWindow(&postProcessConfigWindow);
        menuBar.setOceanEditorWindow(&oceanEditorWindow);
        menuBar.setNavmeshWindow(&navmeshWindow);
        menuBar.setAssetLifecycleWindow(&assetLifecycleWindow);
        menuBar.setWorldSectorWindow(&worldSectorWindow);
        menuBar.setVFXDebugWindow(&vfxDebugWindow);
        menuBar.setAnimationDebugWindow(&animationDebugWindow);
        menuBar.setGIConfigWindow(&giConfigWindow);
        menuBar.setVolumetricFogConfigWindow(&volumetricFogConfigWindow);
        menuBar.setAtmosphereConfigWindow(&atmosphereConfigWindow);
        menuBar.setCloudConfigWindow(&cloudConfigWindow);
        menuBar.setLightStreamingDebugWindow(&lightStreamingDebugWindow);
        menuBar.setPluginManagerWindow(&pluginManagerWindow);
        menuBar.setTaskGraphWindow(&taskGraphWindow);
        menuBar.setInputActionMappingWindow(&inputActionMappingWindow);
        menuBar.setHeightmapGeneratorWindow(&heightmapGeneratorWindow);
        menuBar.setBackgroundRemovalWindow(&backgroundRemovalWindow);
        menuBar.setMemoryDiagnosticsWindow(&memoryDiagnosticsWindow);
        menuBar.setEditorPreferencesWindow(&editorPreferencesWindow);
        subscribeToEvents();
    }

    MainImguiWindow::~MainImguiWindow()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        dispatcher.unsubscribe(sceneClearedToken);
        dispatcher.unsubscribe(sceneLoadedToken);
        dispatcher.unsubscribe(openImportDialogToken);
        dispatcher.unsubscribe(openInputMappingToken);
        dispatcher.unsubscribe(openBackgroundRemovalToken);
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
                volumetricFogConfigWindow.notifySceneLoaded();
                atmosphereConfigWindow.notifySceneLoaded();
                cloudConfigWindow.notifySceneLoaded();
                giConfigWindow.notifySceneLoaded();
                lightStreamingDebugWindow.notifySceneLoaded();
            });

        openImportDialogToken = dispatcher.subscribe<events::application::OpenImportDialogNotification>(
            [this](const events::application::OpenImportDialogNotification&)
            {
                importDialog.openImportDialog();
            });

        openInputMappingToken = dispatcher.subscribe<events::application::OpenInputMappingWindowNotification>(
            [this](const events::application::OpenInputMappingWindowNotification&)
            {
                inputActionMappingWindow.show();
            });

        openBackgroundRemovalToken = dispatcher.subscribe<events::application::OpenBackgroundRemovalNotification>(
            [this](const events::application::OpenBackgroundRemovalNotification& n)
            {
                backgroundRemovalWindow.showWithFile(n.filePath);
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
            audioMixerWindow.draw();
            renderConfigWindow.draw();
            postProcessConfigWindow.draw();
            projectSettingsWindow.draw();
            terrainCreationWindow.draw();
            oceanEditorWindow.draw();
            navmeshWindow.draw();
            assetLifecycleWindow.draw();
            worldSectorWindow.draw();
            vfxDebugWindow.draw();
            animationDebugWindow.draw();
            sculptToolPanel.draw();
            paintToolPanel.draw();
            holeToolPanel.draw();
            caveToolPanel.draw();
            grassDensityPanel.draw();
            meshBrushToolPanel.draw();
            giConfigWindow.draw();
            volumetricFogConfigWindow.draw();
            atmosphereConfigWindow.draw();
            cloudConfigWindow.draw();
            lightStreamingDebugWindow.draw();
            pluginManagerWindow.draw();
            taskGraphWindow.draw();
            inputActionMappingWindow.draw();
            heightmapGeneratorWindow.draw();
            backgroundRemovalWindow.draw();
            memoryDiagnosticsWindow.draw();
            editorPreferencesWindow.draw();
        }
        ImGui::End();
    }
}

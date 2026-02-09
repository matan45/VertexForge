#pragma once
#include "nfd/FileDialog.hpp"

namespace windows
{
    class IBLWindow;
    class EditorCameraWindow;
    class CullingStatsWindow;
    class ImportModalDialog;
    class PhysicsConfigWindow;
    class AudioConfigWindow;
    class RenderConfigWindow;
    class ProjectSettingsWindow;
    class TerrainCreationWindow;

    class MainMenuBar
    {
    private:
        nfd::FileDialog fileDialog;

        IBLWindow* iblWindow = nullptr;
        EditorCameraWindow* editorCameraWindow = nullptr;
        CullingStatsWindow* cullingStatsWindow = nullptr;
        ImportModalDialog* importDialog = nullptr;
        PhysicsConfigWindow* physicsConfigWindow = nullptr;
        AudioConfigWindow* audioConfigWindow = nullptr;
        RenderConfigWindow* renderConfigWindow = nullptr;
        ProjectSettingsWindow* projectSettingsWindow = nullptr;
        TerrainCreationWindow* terrainCreationWindow = nullptr;

    public:
        void draw();

        void setWindows(IBLWindow* ibl, EditorCameraWindow* camera,
                        CullingStatsWindow* culling, ImportModalDialog* import,
                        PhysicsConfigWindow* physicsConfig, AudioConfigWindow* audioConfig,
                        RenderConfigWindow* renderConfig)
        {
            iblWindow = ibl;
            editorCameraWindow = camera;
            cullingStatsWindow = culling;
            importDialog = import;
            physicsConfigWindow = physicsConfig;
            audioConfigWindow = audioConfig;
            renderConfigWindow = renderConfig;
        }

        void setProjectSettingsWindow(ProjectSettingsWindow* window)
        {
            projectSettingsWindow = window;
        }

        void setTerrainCreationWindow(TerrainCreationWindow* window)
        {
            terrainCreationWindow = window;
        }

    private:
        void handleFileMenu();
        void handleSettingsMenu();
        void handleAddMenu();
        void handleScriptsMenu();
        void handleDebug();
        void handlePlayControls();
    };
}

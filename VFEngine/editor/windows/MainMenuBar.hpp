#pragma once
#include "nfd/FileDialog.hpp"

namespace windows
{
    class IBLWindow;
    class EditorCameraWindow;
    class CullingStatsWindow;
    class ImportModalDialog;
    class PhysicsConfigWindow;

    class MainMenuBar
    {
    private:
        nfd::FileDialog fileDialog;

        // References to windows this menu controls
        IBLWindow* iblWindow = nullptr;
        EditorCameraWindow* editorCameraWindow = nullptr;
        CullingStatsWindow* cullingStatsWindow = nullptr;
        ImportModalDialog* importDialog = nullptr;
        PhysicsConfigWindow* physicsConfigWindow = nullptr;

    public:
        void draw();

        void setWindows(IBLWindow* ibl, EditorCameraWindow* camera,
                        CullingStatsWindow* culling, ImportModalDialog* import,
                        PhysicsConfigWindow* physicsConfig)
        {
            iblWindow = ibl;
            editorCameraWindow = camera;
            cullingStatsWindow = culling;
            importDialog = import;
            physicsConfigWindow = physicsConfig;
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

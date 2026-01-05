#pragma once
#include "nfd/FileDialog.hpp"

namespace windows
{
    class IBLWindow;
    class EditorCameraWindow;
    class CullingStatsWindow;
    class ImportModalDialog;

    class MainMenuBar
    {
    private:
        nfd::FileDialog fileDialog;

        // References to windows this menu controls
        IBLWindow* iblWindow = nullptr;
        EditorCameraWindow* editorCameraWindow = nullptr;
        CullingStatsWindow* cullingStatsWindow = nullptr;
        ImportModalDialog* importDialog = nullptr;

    public:
        void draw();

        void setWindows(IBLWindow* ibl, EditorCameraWindow* camera,
                        CullingStatsWindow* culling, ImportModalDialog* import)
        {
            iblWindow = ibl;
            editorCameraWindow = camera;
            cullingStatsWindow = culling;
            importDialog = import;
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

#pragma once
#include "nfd/FileDialog.hpp"
#include "events/EventDispatcher.hpp"
#include <string>

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
    class PostProcessConfigWindow;
    class WaterEditorWindow;
    class OceanEditorWindow;
    class NavmeshWindow;
    class LightBakeWindow;

    class MainMenuBar
    {
    private:
        nfd::FileDialog fileDialog;
        std::string currentSceneName;
        events::SubscriptionToken sceneLoadedToken;
        events::SubscriptionToken sceneClearedToken;

        IBLWindow* iblWindow = nullptr;
        EditorCameraWindow* editorCameraWindow = nullptr;
        CullingStatsWindow* cullingStatsWindow = nullptr;
        ImportModalDialog* importDialog = nullptr;
        PhysicsConfigWindow* physicsConfigWindow = nullptr;
        AudioConfigWindow* audioConfigWindow = nullptr;
        RenderConfigWindow* renderConfigWindow = nullptr;
        ProjectSettingsWindow* projectSettingsWindow = nullptr;
        TerrainCreationWindow* terrainCreationWindow = nullptr;
        WaterEditorWindow* waterEditorWindow = nullptr;
        OceanEditorWindow* oceanEditorWindow = nullptr;
        PostProcessConfigWindow* postProcessConfigWindow = nullptr;
        NavmeshWindow* navmeshWindow = nullptr;
        LightBakeWindow* lightBakeWindow = nullptr;

    public:
        MainMenuBar();
        ~MainMenuBar();
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

        void setWaterEditorWindow(WaterEditorWindow* window)
        {
            waterEditorWindow = window;
        }

        void setOceanEditorWindow(OceanEditorWindow* window)
        {
            oceanEditorWindow = window;
        }

        void setPostProcessConfigWindow(PostProcessConfigWindow* window)
        {
            postProcessConfigWindow = window;
        }

        void setNavmeshWindow(NavmeshWindow* window)
        {
            navmeshWindow = window;
        }

        void setLightBakeWindow(LightBakeWindow* window)
        {
            lightBakeWindow = window;
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

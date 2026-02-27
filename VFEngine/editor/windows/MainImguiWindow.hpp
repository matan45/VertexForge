#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "CullingStatsWindow.hpp"
#include "EditorCameraWindow.hpp"
#include "IBLWindow.hpp"
#include "ImportModalDialog.hpp"
#include "PhysicsConfigWindow.hpp"
#include "AudioConfigWindow.hpp"
#include "RenderConfigWindow.hpp"
#include "PostProcessConfigWindow.hpp"
#include "ProjectSettingsWindow.hpp"
#include "TerrainCreationWindow.hpp"
#include "WaterEditorWindow.hpp"
#include "NavmeshWindow.hpp"
#include "LightBakeWindow.hpp"
#include "SculptToolPanel.hpp"
#include "PaintToolPanel.hpp"
#include "MainMenuBar.hpp"
#include "events/EventDispatcher.hpp"
#include "events/TerrainEvents.hpp"

namespace editor
{
    class EditorCamera;
}

namespace windows
{
    class MainImguiWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        int windowFlags;

        CullingStatsWindow cullingStatsWindow;
        EditorCameraWindow editorCameraWindow;
        IBLWindow iblWindow;
        ImportModalDialog importDialog;
        PhysicsConfigWindow physicsConfigWindow;
        AudioConfigWindow audioConfigWindow;
        RenderConfigWindow renderConfigWindow;
        PostProcessConfigWindow postProcessConfigWindow;
        ProjectSettingsWindow projectSettingsWindow;
        TerrainCreationWindow terrainCreationWindow;
        WaterEditorWindow waterEditorWindow;
        NavmeshWindow navmeshWindow;
        LightBakeWindow lightBakeWindow;
        SculptToolPanel sculptToolPanel;
        PaintToolPanel paintToolPanel;
        MainMenuBar menuBar;

        events::SubscriptionToken sceneClearedToken;
        events::SubscriptionToken sceneLoadedToken;
        events::SubscriptionToken openImportDialogToken;
        events::SubscriptionToken terrainLoadStartedToken;
        bool isLoadingTerrain = false;

    public:
        explicit MainImguiWindow();
        ~MainImguiWindow() override;

        void draw() override;

        void setEditorCamera(editor::EditorCamera* camera)
        {
            editorCameraWindow.setEditorCamera(camera);
        }

    private:
        void subscribeToEvents();
        void onSceneCleared();
        void pollTerrainLoad();
    };
}

#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "config/CullingStatsWindow.hpp"
#include "config/EditorCameraWindow.hpp"
#include "lighting/IBLWindow.hpp"
#include "import/ImportModalDialog.hpp"
#include "config/PhysicsConfigWindow.hpp"
#include "config/AudioConfigWindow.hpp"
#include "config/RenderConfigWindow.hpp"
#include "config/PostProcessConfigWindow.hpp"
#include "config/ProjectSettingsWindow.hpp"
#include "terrain/TerrainCreationWindow.hpp"
#include "WaterEditorWindow.hpp"
#include "config/NavmeshWindow.hpp"
#include "lighting/LightBakeWindow.hpp"
#include "AssetLifecycleWindow.hpp"
#include "WorldSectorWindow.hpp"
#include "VFXDebugWindow.hpp"
#include "AnimationDebugWindow.hpp"
#include "terrain/SculptToolPanel.hpp"
#include "terrain/PaintToolPanel.hpp"
#include "terrain/HoleToolPanel.hpp"
#include "MainMenuBar.hpp"
#include "events/EventDispatcher.hpp"
#include "events/terrain/TerrainEvents.hpp"

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
        AssetLifecycleWindow assetLifecycleWindow;
        WorldSectorWindow worldSectorWindow;
        VFXDebugWindow vfxDebugWindow;
        AnimationDebugWindow animationDebugWindow;
        SculptToolPanel sculptToolPanel;
        PaintToolPanel paintToolPanel;
        HoleToolPanel holeToolPanel;
        MainMenuBar menuBar;

        events::SubscriptionToken sceneClearedToken;
        events::SubscriptionToken sceneLoadedToken;
        events::SubscriptionToken openImportDialogToken;

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
    };
}

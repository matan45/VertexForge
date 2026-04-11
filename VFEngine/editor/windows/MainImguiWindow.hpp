#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "config/CullingStatsWindow.hpp"
#include "config/EditorCameraWindow.hpp"
#include "lighting/IBLWindow.hpp"
#include "import/ImportModalDialog.hpp"
#include "config/PhysicsConfigWindow.hpp"
#include "config/AudioConfigWindow.hpp"
#include "audio/AudioMixerWindow.hpp"
#include "config/RenderConfigWindow.hpp"
#include "config/PostProcessConfigWindow.hpp"
#include "config/ProjectSettingsWindow.hpp"
#include "terrain/TerrainCreationWindow.hpp"
#include "ocean/OceanEditorWindow.hpp"
#include "config/NavmeshWindow.hpp"
#include "asset/AssetLifecycleWindow.hpp"
#include "world/WorldSectorWindow.hpp"
#include "vfx/VFXDebugWindow.hpp"
#include "animation/AnimationDebugWindow.hpp"
#include "terrain/SculptToolPanel.hpp"
#include "terrain/SplineToolPanel.hpp"
#include "terrain/PaintToolPanel.hpp"
#include "terrain/HoleToolPanel.hpp"
#include "terrain/CaveToolPanel.hpp"
#include "vegetation/GrassDensityPanel.hpp"
#include "meshbrush/MeshBrushToolPanel.hpp"
#include "config/GIConfigWindow.hpp"
#include "config/VolumetricFogConfigWindow.hpp"
#include "config/AtmosphereConfigWindow.hpp"
#include "config/CloudConfigWindow.hpp"
#include "config/LightStreamingDebugWindow.hpp"
#include "plugin/PluginManagerWindow.hpp"
#include "debug/TaskGraphWindow.hpp"
#include "config/InputActionMappingWindow.hpp"
#include "procedural/HeightmapGeneratorWindow.hpp"
#include "imageprocessing/BackgroundRemovalWindow.hpp"
#include "debug/MemoryDiagnosticsWindow.hpp"
#include "config/EditorPreferencesWindow.hpp"
#include "config/EditorSettingsWindow.hpp"
#include "config/EnvironmentWindow.hpp"
#include "weather/WeatherEditorWindow.hpp"
#include "EngineToolbar.hpp"
#include "StatusBar.hpp"
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
        AudioMixerWindow audioMixerWindow;
        RenderConfigWindow renderConfigWindow;
        PostProcessConfigWindow postProcessConfigWindow;
        ProjectSettingsWindow projectSettingsWindow;
        TerrainCreationWindow terrainCreationWindow;
        OceanEditorWindow oceanEditorWindow;
        NavmeshWindow navmeshWindow;
        AssetLifecycleWindow assetLifecycleWindow;
        WorldSectorWindow worldSectorWindow;
        VFXDebugWindow vfxDebugWindow;
        AnimationDebugWindow animationDebugWindow;
        SculptToolPanel sculptToolPanel;
        SplineToolPanel splineToolPanel;
        PaintToolPanel paintToolPanel;
        HoleToolPanel holeToolPanel;
        CaveToolPanel caveToolPanel;
        GrassDensityPanel grassDensityPanel;
        MeshBrushToolPanel meshBrushToolPanel;
        GIConfigWindow giConfigWindow;
        VolumetricFogConfigWindow volumetricFogConfigWindow;
        AtmosphereConfigWindow atmosphereConfigWindow;
        CloudConfigWindow cloudConfigWindow;
        LightStreamingDebugWindow lightStreamingDebugWindow;
        PluginManagerWindow pluginManagerWindow;
        TaskGraphWindow taskGraphWindow;
        InputActionMappingWindow inputActionMappingWindow;
        HeightmapGeneratorWindow heightmapGeneratorWindow;
        BackgroundRemovalWindow backgroundRemovalWindow;
        MemoryDiagnosticsWindow memoryDiagnosticsWindow;
        EditorPreferencesWindow editorPreferencesWindow;
        EditorSettingsWindow editorSettingsWindow;
        EnvironmentWindow environmentWindow;
        WeatherEditorWindow weatherEditorWindow;
        EngineToolbar engineToolbar;
        StatusBar statusBar;
        MainMenuBar menuBar;

        events::SubscriptionToken sceneClearedToken;
        events::SubscriptionToken sceneLoadedToken;
        events::SubscriptionToken openImportDialogToken;
        events::SubscriptionToken openInputMappingToken;
        events::SubscriptionToken openProjectSettingsToken;
        events::SubscriptionToken openBackgroundRemovalToken;
        events::SubscriptionToken settingsChangedToken;

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

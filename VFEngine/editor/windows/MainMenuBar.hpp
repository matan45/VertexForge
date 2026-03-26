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
    class AudioMixerWindow;
    class RenderConfigWindow;
    class ProjectSettingsWindow;
    class TerrainCreationWindow;
    class PostProcessConfigWindow;
    class OceanEditorWindow;
    class NavmeshWindow;
    class AssetLifecycleWindow;
    class WorldSectorWindow;
    class VFXDebugWindow;
    class AnimationDebugWindow;
    class GIConfigWindow;
    class VolumetricFogConfigWindow;
    class AtmosphereConfigWindow;
    class CloudConfigWindow;
    class LightStreamingDebugWindow;
    class PluginManagerWindow;
    class InputActionMappingWindow;
    class TaskGraphWindow;
    class HeightmapGeneratorWindow;
    class BackgroundRemovalWindow;
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
        AudioMixerWindow* audioMixerWindow = nullptr;
        RenderConfigWindow* renderConfigWindow = nullptr;
        ProjectSettingsWindow* projectSettingsWindow = nullptr;
        TerrainCreationWindow* terrainCreationWindow = nullptr;
        OceanEditorWindow* oceanEditorWindow = nullptr;
        PostProcessConfigWindow* postProcessConfigWindow = nullptr;
        NavmeshWindow* navmeshWindow = nullptr;
        AssetLifecycleWindow* assetLifecycleWindow = nullptr;
        WorldSectorWindow* worldSectorWindow = nullptr;
        VFXDebugWindow* vfxDebugWindow = nullptr;
        AnimationDebugWindow* animationDebugWindow = nullptr;
        GIConfigWindow* giConfigWindow = nullptr;
        VolumetricFogConfigWindow* volumetricFogConfigWindow = nullptr;
        AtmosphereConfigWindow* atmosphereConfigWindow = nullptr;
        CloudConfigWindow* cloudConfigWindow = nullptr;
        LightStreamingDebugWindow* lightStreamingDebugWindow = nullptr;
        PluginManagerWindow* pluginManagerWindow = nullptr;
        InputActionMappingWindow* inputActionMappingWindow = nullptr;
        TaskGraphWindow* taskGraphWindow = nullptr;
        HeightmapGeneratorWindow* heightmapGeneratorWindow = nullptr;
        BackgroundRemovalWindow* backgroundRemovalWindow = nullptr;
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

        void setAssetLifecycleWindow(AssetLifecycleWindow* window)
        {
            assetLifecycleWindow = window;
        }

        void setWorldSectorWindow(WorldSectorWindow* window)
        {
            worldSectorWindow = window;
        }

        void setVFXDebugWindow(VFXDebugWindow* window)
        {
            vfxDebugWindow = window;
        }

        void setAnimationDebugWindow(AnimationDebugWindow* window)
        {
            animationDebugWindow = window;
        }

        void setGIConfigWindow(GIConfigWindow* window)
        {
            giConfigWindow = window;
        }

        void setVolumetricFogConfigWindow(VolumetricFogConfigWindow* window)
        {
            volumetricFogConfigWindow = window;
        }

        void setAtmosphereConfigWindow(AtmosphereConfigWindow* window)
        {
            atmosphereConfigWindow = window;
        }

        void setCloudConfigWindow(CloudConfigWindow* window)
        {
            cloudConfigWindow = window;
        }

        void setLightStreamingDebugWindow(LightStreamingDebugWindow* window)
        {
            lightStreamingDebugWindow = window;
        }

        void setAudioMixerWindow(AudioMixerWindow* window)
        {
            audioMixerWindow = window;
        }

        void setPluginManagerWindow(PluginManagerWindow* window)
        {
            pluginManagerWindow = window;
        }

        void setInputActionMappingWindow(InputActionMappingWindow* window)
        {
            inputActionMappingWindow = window;
        }

        void setTaskGraphWindow(TaskGraphWindow* window)
        {
            taskGraphWindow = window;
        }

        void setHeightmapGeneratorWindow(HeightmapGeneratorWindow* window)
        {
            heightmapGeneratorWindow = window;
        }

        void setBackgroundRemovalWindow(BackgroundRemovalWindow* window)
        {
            backgroundRemovalWindow = window;
        }

    private:
        void handleFileMenu();
        void handleSettingsMenu();
        void handleAddMenu();
        void handleScriptsMenu();
        void handleToolsMenu();
        void handleDebug();
        void handlePlayControls();
    };
}

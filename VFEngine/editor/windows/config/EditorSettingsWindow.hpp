#pragma once

namespace windows
{
    class ProjectSettingsWindow;
    class EditorCameraWindow;
    class PhysicsConfigWindow;
    class AudioConfigWindow;
    class AudioMixerWindow;
    class RenderConfigWindow;
    class InputActionMappingWindow;
    class PluginManagerWindow;

    class EditorSettingsWindow
    {
    public:
        enum Category
        {
            Project = 0,
            EditorCamera,
            Physics,
            AudioConfig,
            AudioMixer,
            Rendering,
            InputMapping,
            Plugins,
            COUNT
        };

    private:
        bool visible = false;
        int selectedCategory = 0;

        ProjectSettingsWindow* projectSettings = nullptr;
        EditorCameraWindow* editorCameraWindow = nullptr;
        PhysicsConfigWindow* physicsConfig = nullptr;
        AudioConfigWindow* audioConfig = nullptr;
        AudioMixerWindow* audioMixer = nullptr;
        RenderConfigWindow* renderConfig = nullptr;
        InputActionMappingWindow* inputMapping = nullptr;
        PluginManagerWindow* pluginManager = nullptr;

    public:
        void setWindows(ProjectSettingsWindow* project, EditorCameraWindow* camera,
                        PhysicsConfigWindow* physics, AudioConfigWindow* audio,
                        AudioMixerWindow* mixer, RenderConfigWindow* render,
                        InputActionMappingWindow* input, PluginManagerWindow* plugins);

        void draw();
        void show(int category = -1);
        bool isVisible() const { return visible; }

    private:
        void drawCategoryList();
        void drawCategoryContent();
    };
}

#pragma once

namespace windows
{
    class AtmosphereConfigWindow;
    class CloudConfigWindow;
    class VolumetricFogConfigWindow;
    class WeatherEditorWindow;

    class EnvironmentWindow
    {
    public:
        enum Tab
        {
            Atmosphere = 0,
            Clouds,
            VolumetricFog,
            Weather,
            COUNT
        };

    private:
        bool visible = false;
        int selectedTab = 0;

        AtmosphereConfigWindow* atmosphereWindow = nullptr;
        CloudConfigWindow* cloudWindow = nullptr;
        VolumetricFogConfigWindow* fogWindow = nullptr;
        WeatherEditorWindow* weatherWindow = nullptr;

    public:
        void setWindows(AtmosphereConfigWindow* atmo, CloudConfigWindow* cloud,
                        VolumetricFogConfigWindow* fog, WeatherEditorWindow* weather);

        void draw();
        void show(int tab = -1);
        bool isVisible() const { return visible; }

    private:
        void drawTabList();
        void drawTabContent();
    };
}

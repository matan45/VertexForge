#pragma once
#include "cloud/CloudSettings.hpp"

namespace windows
{
    class CloudConfigWindow
    {
    private:
        bool visible = false;
        render::cloud::CloudSettings settings;
        bool settingsLoaded = false;
        bool isDirty = false;

        void loadSettings();
        void applySettings();

        void drawLayerSection();
        void drawDensitySection();
        void drawNoiseSection();
        void drawWindSection();
        void drawLightingSection();
        void drawPerformanceSection();

    public:
        void show() { visible = true; settingsLoaded = false; }
        void notifySceneLoaded() { settingsLoaded = false; isDirty = false; }
        void draw();
        bool isVisible() const { return visible; }
    };
}

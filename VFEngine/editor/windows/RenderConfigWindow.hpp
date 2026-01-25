#pragma once
#include "types/RenderSettings.hpp"

namespace windows
{
    class RenderConfigWindow
    {
    private:
        bool visible = false;
        types::RenderSettings settings = types::RenderSettings::createDefault();
        bool settingsLoaded = false;
        bool isDirty = false;

        void drawShadowSection();
        void loadFromScene();
        void saveToScene();
        void resetToDefaults();
        void applySettings();

    public:
        void draw();
        void show();
    };
}

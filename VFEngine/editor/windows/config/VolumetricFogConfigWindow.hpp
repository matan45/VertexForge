#pragma once
#include "postprocess/PostProcessTypes.hpp"

namespace windows
{
    class VolumetricFogConfigWindow
    {
    private:
        bool visible = false;
        postprocess::VolumetricFogSettings settings;
        bool settingsLoaded = false;
        bool isDirty = false;

        void drawDensitySection();
        void drawHeightFogSection();
        void drawScatteringSection();
        void drawGeneralSection();

        void loadSettings();
        void applySettings();
        void resetToDefaults();

    public:
        void draw();
        void show();
        void notifySceneLoaded();
    };
}

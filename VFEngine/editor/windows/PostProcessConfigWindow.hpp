#pragma once
#include "postprocess/PostProcessTypes.hpp"

namespace windows
{
    class PostProcessConfigWindow
    {
    private:
        bool visible = false;
        postprocess::PostProcessSettings settings = postprocess::PostProcessSettings::createDefault();
        bool settingsLoaded = false;
        bool isDirty = false;

        void drawToneMappingSection();
        void drawFXAASection();
        void drawBloomSection();
        void drawVignetteSection();
        void drawChromaticAberrationSection();
        void drawFilmGrainSection();
        void drawGodRaysSection();
        void drawDepthOfFieldSection();
        void drawVolumetricFogSection();
        void loadSettings();
        void applySettings();
        void resetToDefaults();

    public:
        void draw();
        void show();
        void notifySceneLoaded();
    };
}

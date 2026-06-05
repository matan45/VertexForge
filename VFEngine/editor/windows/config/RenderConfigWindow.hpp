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

        void drawPresetSection();
        void markDirty();
        void drawCullingSection();
        void drawShadowSection();
        void drawShadowQualitySettings();
        void drawShadowBiasSettings();
        void drawShadowFilterSettings();
        void drawRTShadowSection();
        void drawShadowDebugSection();
        void drawShadowStatistics();
        void drawTerrainSection();
        void drawVFXLODSection();
        void drawAnimationLODSection();
        void loadFromScene();
        void saveToScene();
        void resetToDefaults();
        void applySettings();
        void applyVFXLODSettings();
        void applyAnimationLODSettings();

    public:
        void draw();
        void drawContent();
        void show();
        void notifySceneLoaded();
    };
}

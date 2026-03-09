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

        void drawCullingSection();
        void drawShadowSection();
        void drawShadowQualitySettings();
        void drawShadowCSMSettings();
        void drawShadowBiasSettings();
        void drawShadowFilterSettings();
        void drawShadowDebugSection();
        void drawShadowStatistics();
        void drawTerrainSection();
        void drawVFXLODSection();
        void drawAnimationLODSection();
        void drawVegetationSection();
        void loadFromScene();
        void saveToScene();
        void resetToDefaults();
        void applySettings();

    public:
        void draw();
        void show();
    };
}

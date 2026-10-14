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
        // VK-1654: the preset combo replaced settings.gi and no write has delivered it yet, so the
        // next write keeps it instead of the scene's (see mergedForWrite / writeSceneSettings).
        bool giFromPreset = false;

        void drawPresetSection();
        void drawDisplaySection();
        void markDirty();
        void drawCullingSection();
        void drawShadowSection();
        void drawShadowQualitySettings();
        void drawShadowBiasSettings();
        void drawShadowFilterSettings();
        void drawDirectionalClipmapSettings();
        void drawRTShadowSection();
        void drawShadowDebugSection();
        void drawShadowStatistics();
        void drawTerrainSection();
        void drawVirtualTextureSection();
        void drawVFXLODSection();
        void drawAnimationLODSection();
        void loadFromScene();
        void saveToScene();
        types::RenderSettings mergedForWrite() const;
        void writeSceneSettings();
        void resetToDefaults();
        void applySettings();
        void applyVFXLODSettings();
        void applyAnimationLODSettings();
        void publishDisplaySettings();

    public:
        void draw();
        void drawContent();
        void show();
        void notifySceneLoaded();
        void notifyRenderSettingsChanged();
    };
}

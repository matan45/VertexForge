#pragma once

#include "data/WaterData.hpp"
#include "data/EntityHandle.hpp"
#include "nfd/FileDialog.hpp"

namespace windows
{
    class WaterEditorWindow
    {
    private:
        bool visible = false;

        // Creation params
        int tilesX = 4;
        int tilesZ = 4;
        float worldTileSize = 32.0f;
        float waterHeight = 0.0f;
        float waveIntensity = 1.0f;
        bool physicsEnabled = true;
        float shallowColor[4] = {0.0f, 0.5f, 0.7f, 0.6f};
        float deepColor[4] = {0.0f, 0.1f, 0.3f, 0.9f};

        // Settings (when water exists)
        bool hasWater = false;
        services::EntityHandle waterEntity;
        services::WaterGlobalSettingsData globalSettings;
        bool settingsDirty = false;

        nfd::FileDialog fileDialog;

    public:
        void draw();
        void show();

    private:
        void drawCreationSection();
        void drawSettingsSection();
        void drawInfoSection();
        void createWater();
        void deleteWater();
        void applySettings();
        void resetCreationDefaults();
        void refreshWaterState();
        void saveWater();
        void loadWater();
    };
}

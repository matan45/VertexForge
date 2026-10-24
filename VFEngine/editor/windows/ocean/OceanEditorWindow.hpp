#pragma once

#include "data/OceanData.hpp"
#include "data/EntityHandle.hpp"
#include "nfd/FileDialog.hpp"

namespace windows
{
    class OceanEditorWindow
    {
    private:
        bool visible = false;

        float waterHeight = 0.0f;
        bool physicsEnabled = true;
        float shallowColor[4] = {0.0f, 0.4f, 0.6f, 0.7f};
        float deepColor[4] = {0.0f, 0.05f, 0.2f, 0.95f};

        bool hasOcean = false;
        services::EntityHandle oceanEntity;

        services::OceanFFTConfigData oceanConfig;
        bool oceanConfigDirty = false;

        services::OceanVisualSettings visualSettings;
        bool visualSettingsDirty = false;

        services::OceanPhysicsSettings physicsSettings;
        bool physicsSettingsDirty = false;

        // Weather-driven sea state
        bool weatherDriven = false;
        float weatherResponse = 1.0f;
        int seaStatePresetIndex = 1;
        float seaStateTransitionSeconds = 10.0f;

        // VK-1658: OceanComponent::isActive, i.e. the sea surface is drawn. Applied when toggled.
        bool surfaceVisible = true;

        nfd::FileDialog fileDialog;

    public:
        void draw();
        void show();

    private:
        void drawCreationSection();
        void drawSettingsSection();
        void drawOceanFFTSection();
        void drawShoreDepthFieldStatus();   // VK-1605
        void drawWaterBodiesSection();      // VK-1607
        void drawInfoSection();
        void createOcean();
        void deleteOcean();
        void applyOceanConfig();
        void applyVisualSettings();
        void applyPhysicsSettings();
        void applySurfaceVisible();         // VK-1658
        void refreshOceanState();
        void syncWithEngine();              // VK-1658
        void pullOceanState(bool keepEdits);  // VK-1658
        void saveOcean();
        void loadOcean();
    };
}

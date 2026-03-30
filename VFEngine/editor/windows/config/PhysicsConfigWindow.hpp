#pragma once

#include "types/PhysicsTypes.hpp"

namespace windows
{
    class PhysicsConfigWindow
    {
    private:
        bool visible = false;
        types::PhysicsSettings settings = types::PhysicsSettings::createDefault();
        bool settingsLoaded = false;

        char newLayerName[64] = "";

        bool isDirty = false;

        void drawGravitySection();
        void drawSimulationSection();
        void drawSleepSection();
        void drawLayersSection();
        void drawCollisionMatrixSection();
        void drawVFXCollisionSection();

        void loadFromScene();
        void saveToScene();
        void applySettings();
        void resetToDefaults();

    public:
        void draw();
        void drawContent();
        void show();
    };
}

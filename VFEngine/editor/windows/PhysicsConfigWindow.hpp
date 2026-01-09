#pragma once

#include "../../utilities/types/PhysicsTypes.hpp"
#include <string>

namespace windows
{
    class PhysicsConfigWindow
    {
    private:
        bool visible = false;
        types::PhysicsSettings settings = types::PhysicsSettings::createDefault();
        bool settingsLoaded = false;

        // For adding new layers
        char newLayerName[64] = "";

        // Track if settings have been modified
        bool isDirty = false;

        void drawGravitySection();
        void drawSimulationSection();
        void drawSleepSection();
        void drawLayersSection();
        void drawCollisionMatrixSection();

        void loadFromScene();
        void saveToScene();
        void applySettings();
        void resetToDefaults();

    public:
        void draw();

        void show();
        void hide() { visible = false; }
        bool isVisible() const { return visible; }
    };
}

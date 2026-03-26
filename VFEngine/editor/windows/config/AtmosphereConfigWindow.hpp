#pragma once
#include "atmosphere/AtmosphereSettings.hpp"

namespace windows
{
    class AtmosphereConfigWindow
    {
    private:
        bool visible = false;
        render::atmosphere::AtmosphereSettings settings;
        bool settingsLoaded = false;
        bool isDirty = false;

        void loadSettings();
        void applySettings();
        void resetToDefaults();

        void drawPlanetSection();
        void drawRayleighSection();
        void drawMieSection();
        void drawOzoneSection();
        void drawSunSection();
        void drawDayNightSection();
        void drawMoonSection();
        void drawStarsSection();
        void drawAerialSection();

    public:
        void show();
        void notifySceneLoaded();
        void draw();
        bool isVisible() const { return visible; }
    };
}

#pragma once

#include "types/AudioTypes.hpp"

namespace windows
{
    class AudioConfigWindow
    {
    private:
        bool visible = false;
        types::AudioSettings settings = types::AudioSettings::createDefault();
        bool settingsLoaded = false;
        bool isDirty = false;

        void drawListenerSection();
        void drawDistanceModelSection();

        void loadFromScene();
        void saveToScene();
        void applySettings();
        void resetToDefaults();

    public:
        void draw();
        void show();
    };
}

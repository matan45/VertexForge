#pragma once

#include "weather/WeatherTypes.hpp"
#include "weather/WeatherAudioController.hpp"

namespace windows
{
    class WeatherEditorWindow
    {
    private:
        bool visible = false;
        bool settingsLoaded = false;

        weather::WeatherState currentState;
        float snowAccumulation = 0.0f;
        float wetness = 0.0f;
        float transitionProgress = 0.0f;
        bool weatherEnabled = false;
        bool scheduleEnabled = false;
        float transitionDuration = 30.0f;

        void loadState();
        void drawPresetButtons();
        void drawCurrentState();
        void drawManualControls();
        void drawScheduleControls();
        void drawAudioConfig();

    public:
        void show();
        void draw();
        void drawContent();
        bool isVisible() const { return visible; }
    };
}

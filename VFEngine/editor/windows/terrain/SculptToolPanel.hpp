#pragma once

#include "events/EventTypes.hpp"

#include <string>

namespace windows
{
    class SculptToolPanel
    {
    private:
        bool visible = false;
        int selectedBrushType = 0;
        float brushRadius = 5.0f;
        float brushStrength = 10.0f;
        int falloffIndex = 2;
        int shapeIndex = 0;

        // Stamp brush state
        std::string stampImagePath;
        float stampRotation = 0.0f;
        float stampScale = 1.0f;
        int stampMode = 0; // 0=Add, 1=Subtract
        bool stampLoaded = false;

        // Erosion brush state
        float talusAngle = 45.0f;

        // Terrace brush state
        float terraceStepHeight = 2.0f;
        float terraceSharpness = 0.5f;

        // Ramp state
        float rampWidth = 5.0f;
        float rampFalloff = 2.0f;

        events::SubscriptionToken sculptModeToken;
        events::SubscriptionToken brushTypeToken;
        events::SubscriptionToken brushParamsToken;
        events::SubscriptionToken stampImageToken;

        bool subscribed = false;

    public:
        SculptToolPanel() = default;
        ~SculptToolPanel();

        void draw();

    private:
        void subscribe();
    };
}

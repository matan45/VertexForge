#pragma once

#include "events/EventTypes.hpp"
#include "terrain/BrushTypes.hpp"

#include <cstdint>
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

        // Hydraulic erosion brush state
        float hydraulicRainRate = 0.35f;
        float hydraulicSedimentCapacity = 1.2f;
        float hydraulicEvaporation = 0.015f;
        float hydraulicHardness = 0.5f;
        float hydraulicSmoothing = 0.2f;
        int hydraulicIterations = 24;

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

        // VK-1616: the mode-enter pull and the params-changed push used to be two hand-maintained
        // copies of the same field list, which is how VK-1613's stampMode readout ended up lying.
        // Six new hydraulic fields would have been six more chances to repeat it, so both paths now
        // go through here.
        void applyParams(const terrain::BrushParams& params);
    };
}

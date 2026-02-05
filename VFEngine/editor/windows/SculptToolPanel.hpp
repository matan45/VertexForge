#pragma once

#include "events/EventTypes.hpp"

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

        events::SubscriptionToken sculptModeToken;
        events::SubscriptionToken brushTypeToken;
        events::SubscriptionToken brushParamsToken;

        bool subscribed = false;

    public:
        SculptToolPanel() = default;
        ~SculptToolPanel();

        void draw();

    private:
        void subscribe();
    };
}

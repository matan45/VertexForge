#pragma once

#include "events/EventTypes.hpp"

namespace windows
{
    class HoleToolPanel
    {
    private:
        bool visible = false;
        float brushRadius = 5.0f;
        int falloffIndex = 2;
        int shapeIndex = 0;

        events::SubscriptionToken holeModeToken;
        events::SubscriptionToken holeParamsToken;

        bool subscribed = false;

    public:
        HoleToolPanel() = default;
        ~HoleToolPanel();

        void draw();

    private:
        void subscribe();
    };
}

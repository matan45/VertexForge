#pragma once

#include "events/EventTypes.hpp"

namespace events { class EventDispatcher; }

namespace windows
{
    class CaveToolPanel
    {
    private:
        bool visible = false;
        int selectedBrushType = 0;
        float brushRadius = 5.0f;
        float brushStrength = 10.0f;
        int falloffIndex = 2;
        int shapeIndex = 0;

        events::SubscriptionToken caveModeToken;
        events::SubscriptionToken brushTypeToken;
        events::SubscriptionToken brushParamsToken;

        bool subscribed = false;

    public:
        CaveToolPanel() = default;
        ~CaveToolPanel();

        void draw();

    private:
        void subscribe();
        void drawBrushTypeSelector(events::EventDispatcher& dispatcher);
        void drawBrushParams(events::EventDispatcher& dispatcher);
    };
}

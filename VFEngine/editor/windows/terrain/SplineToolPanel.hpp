#pragma once

#include "events/EventTypes.hpp"

namespace windows
{
    class SplineToolPanel
    {
    private:
        bool visible = false;
        float corridorWidth = 5.0f;
        float falloffWidth = 3.0f;
        float embankmentHeight = 0.0f;
        uint32_t pointCount = 0;

        events::SubscriptionToken modeToken;
        events::SubscriptionToken pointToken;
        events::SubscriptionToken paramsToken;

        bool subscribed = false;

    public:
        SplineToolPanel() = default;
        ~SplineToolPanel();

        void draw();

    private:
        void subscribe();
    };
}

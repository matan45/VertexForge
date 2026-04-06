#pragma once

#include "events/EventTypes.hpp"

namespace windows
{
    class SplineToolPanel
    {
    private:
        bool visible = false;
        int splineMode = 0; // 0=Sculpt, 1=Paint
        float corridorWidth = 5.0f;
        float falloffWidth = 3.0f;
        float embankmentHeight = 0.0f;
        int paintLayer = 1;
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

#pragma once

#include "events/EventTypes.hpp"
#include <glm/glm.hpp>
#include <vector>

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
        uint32_t cachedPointCount = 0;
        float cachedCorridorWidth = 0.0f;
        std::vector<glm::vec3> cachedSamples;

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

#pragma once

#include "events/EventTypes.hpp"

namespace windows
{
    class GrassDensityPanel
    {
    private:
        bool visible = false;
        int selectedBrushType = 0;
        float brushRadius = 5.0f;
        float brushStrength = 10.0f;
        float brushOpacity = 1.0f;
        int falloffIndex = 2;
        int shapeIndex = 0;

        events::SubscriptionToken modeToken;
        events::SubscriptionToken brushTypeToken;
        events::SubscriptionToken brushParamsToken;

        bool subscribed = false;

    public:
        GrassDensityPanel() = default;
        ~GrassDensityPanel();

        void draw();
        void setVisible(bool v) { visible = v; }
        [[nodiscard]] bool isVisible() const { return visible; }

    private:
        void subscribe();
    };
}

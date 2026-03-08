#pragma once

#include "events/EventTypes.hpp"

namespace windows
{
    class VegetationPlacementPanel
    {
    private:
        bool visible = false;
        int selectedBrushType = 0; // 0=Scatter, 1=Erase
        float brushRadius = 10.0f;
        float density = 0.5f;
        float minScale = 0.8f;
        float maxScale = 1.2f;
        float randomRotation = 1.0f;
        int selectedSpecies = 0;

        events::SubscriptionToken modeToken;
        bool subscribed = false;

    public:
        VegetationPlacementPanel() = default;
        ~VegetationPlacementPanel();

        void draw();
        void setVisible(bool v) { visible = v; }
        [[nodiscard]] bool isVisible() const { return visible; }

    private:
        void subscribe();
    };
}

#pragma once

#include "events/EventTypes.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace windows
{
    class VegetationPlacementPanel
    {
    private:
        bool visible = false;
        int selectedBrushType = 0; // 0=Place, 1=Erase, 2=Spread
        float brushRadius = 10.0f;
        float density = 0.5f;
        float minScale = 0.8f;
        float maxScale = 1.2f;
        float randomRotation = 1.0f;
        uint32_t selectedSpeciesId = 0;

        // Temp storage for combo rendering
        std::vector<uint32_t> speciesIds;
        std::vector<std::string> speciesNames;

        events::SubscriptionToken modeToken;
        bool subscribed = false;
        bool needsInitialParamSend = true;

    public:
        VegetationPlacementPanel() = default;
        ~VegetationPlacementPanel();

        void draw();
        void setVisible(bool v) { visible = v; }
        [[nodiscard]] bool isVisible() const { return visible; }

    private:
        void subscribe();
        void sendBrushParams();
    };
}

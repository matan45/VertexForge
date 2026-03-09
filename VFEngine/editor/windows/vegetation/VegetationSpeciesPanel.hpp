#pragma once

#include "vegetation/VegetationSpecies.hpp"
#include "events/EventTypes.hpp"
#include <unordered_map>
#include <cstdint>
#include <string>

namespace windows
{
    class VegetationSpeciesPanel
    {
    private:
        bool visible = false;
        int selectedSpeciesId = -1;

        // Cached species list
        std::unordered_map<uint32_t, vegetation::VegetationSpeciesConfig> cachedSpecies;
        bool cacheValid = false;

        events::SubscriptionToken modeToken;
        bool subscribed = false;

    public:
        VegetationSpeciesPanel() = default;
        ~VegetationSpeciesPanel();

        void draw();
        void setVisible(bool v) { visible = v; }
        [[nodiscard]] bool isVisible() const { return visible; }

    private:
        void subscribe();
        void refreshCache();
    };
}

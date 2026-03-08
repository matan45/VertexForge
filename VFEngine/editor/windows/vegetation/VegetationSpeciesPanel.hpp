#pragma once

#include "vegetation/VegetationSpecies.hpp"
#include <unordered_map>
#include <cstdint>

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

    public:
        VegetationSpeciesPanel() = default;

        void draw();
        void setVisible(bool v) { visible = v; }
        [[nodiscard]] bool isVisible() const { return visible; }

    private:
        void refreshCache();
    };
}

#pragma once
#include "../vegetation/GrassConfig.hpp"
#include "../vegetation/VegetationTypes.hpp"
#include "../vegetation/VegetationScatterTypes.hpp"
#include <vector>
#include <cstdint>

namespace components
{
    struct GrassComponent
    {
        vegetation::GrassRenderConfig config;
        std::vector<vegetation::BillboardPaletteEntry> billboardPalette;
        vegetation::ScatterProfile scatterProfile; // VK-1581 procedural scatter rules
        bool enabled = true;
    };

    struct VegetationComponent
    {
        bool enabled = true;
    };
}

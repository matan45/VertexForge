#pragma once
#include "../vegetation/GrassConfig.hpp"
#include "../vegetation/VegetationTypes.hpp"
#include <array>
#include <vector>
#include <cstdint>

namespace components
{
    struct GrassComponent
    {
        vegetation::GrassRenderConfig config;
        std::vector<vegetation::BillboardPaletteEntry> billboardPalette;
        bool enabled = true;
    };

    struct VegetationComponent
    {
        bool enabled = true;
        std::array<vegetation::VegetationTypeConfig, vegetation::VEGETATION_TYPE_COUNT> typeConfigs = {};
    };
}

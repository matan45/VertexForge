#pragma once
#include "../vegetation/GrassConfig.hpp"
#include <cstdint>

namespace components
{
    struct GrassComponent
    {
        vegetation::GrassRenderConfig config;
        bool enabled = true;
    };

    struct VegetationComponent
    {
        bool enabled = true;
        uint32_t activeSpeciesCount = 0;
    };
}

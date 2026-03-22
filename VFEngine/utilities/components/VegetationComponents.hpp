#pragma once
#include "../vegetation/GrassConfig.hpp"
#include "../vegetation/VegetationTypes.hpp"
#include <array>
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
        std::array<vegetation::VegetationTypeConfig, vegetation::VEGETATION_TYPE_COUNT> typeConfigs = {{
            {vegetation::VegetationType::Grass, 0.3f, 0.8f, 1.0f, 0.7f, 1.0f, 80.0f, 120.0f, {0.2f, 0.6f, 0.1f, 1.0f}},
            {vegetation::VegetationType::Billboard, 0.3f, 1.0f, 1.0f, 0.5f, 0.3f, 60.0f, 100.0f, {1.0f, 1.0f, 1.0f, 1.0f}}
        }};
    };
}

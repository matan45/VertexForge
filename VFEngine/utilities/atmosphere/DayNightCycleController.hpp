#pragma once
#include "AtmosphereSettings.hpp"
#include <cmath>

namespace render::atmosphere
{
    class DayNightCycleController
    {
    public:
        void tick(float deltaTime, AtmosphereSettings& settings);
    };
}

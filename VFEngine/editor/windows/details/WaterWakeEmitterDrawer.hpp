#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    // VK-1606: inspector for WaterWakeEmitterComponent — an authored source of ripples on the ocean
    // surface (bow wave, propeller wash, dripping torch).
    class WaterWakeEmitterDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawSettings(services::WaterWakeEmitterComponentData& emitterData);
    };
}

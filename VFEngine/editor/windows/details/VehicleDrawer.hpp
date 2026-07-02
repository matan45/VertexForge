#pragma once

#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    class VehicleDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawPresetControls(services::VehicleComponentData& data);
        bool drawEngineControls(types::VehicleConfig& config);
        bool drawCollisionControls(types::VehicleConfig& config);
        bool drawWheelControls(types::VehicleConfig& config);
        bool drawDifferentialControls(types::VehicleConfig& config);
    };
}

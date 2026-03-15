#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    class ReverbZoneDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawShapeSettings(services::ReverbZoneData& data);
        bool drawPresetSettings(services::ReverbZoneData& data);
        bool drawZoneBehavior(services::ReverbZoneData& data);
        bool drawDebugSettings(services::ReverbZoneData& data);
    };
}

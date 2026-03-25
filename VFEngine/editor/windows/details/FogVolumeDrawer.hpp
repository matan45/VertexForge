#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    class FogVolumeDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawShapeSettings(services::FogVolumeData& data);
        bool drawDensitySettings(services::FogVolumeData& data);
        bool drawBlendSettings(services::FogVolumeData& data);
        bool drawDebugSettings(services::FogVolumeData& data);
    };
}

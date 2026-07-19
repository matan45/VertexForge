#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    class ReflectionProbeDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawShapeSettings(services::ReflectionProbeData& data);
        bool drawBlendSettings(services::ReflectionProbeData& data);
        bool drawCaptureSettings(services::ReflectionProbeData& data);
        bool drawDebugSettings(services::ReflectionProbeData& data);
        void drawBakeControls(services::EntityHandle handle);
    };
}

#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    class VFXDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawVFXFilePath(services::VFXData& vfxData);
        bool drawSettings(services::VFXData& vfxData);
    };
}

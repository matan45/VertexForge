#pragma once
#include "data/EntityHandle.hpp"

namespace windows::details
{
    class VolumetricVolumeDrawer
    {
    public:
        bool draw(services::EntityHandle handle);
    };
}

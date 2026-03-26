#pragma once
#include "data/EntityHandle.hpp"

namespace windows::details
{
    class NavmeshModifierVolumeDrawer
    {
    public:
        bool draw(services::EntityHandle handle);
    };
}

#pragma once
#include "data/EntityHandle.hpp"

namespace windows::details
{
    class OceanDrawer
    {
    public:
        bool draw(services::EntityHandle handle);
    };
}

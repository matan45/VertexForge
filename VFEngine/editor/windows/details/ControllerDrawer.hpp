#pragma once
#include "data/EntityHandle.hpp"

namespace windows::details
{
    class ControllerDrawer
    {
    public:
        bool draw(services::EntityHandle handle);
    };
}

#pragma once
#include "data/EntityHandle.hpp"

namespace windows::details
{
    class NavInvokerDrawer
    {
    public:
        bool draw(services::EntityHandle handle);
    };
}

#pragma once
#include "data/EntityHandle.hpp"

namespace windows::details
{
    class MetaComponentDrawer
    {
    public:
        void draw(services::EntityHandle handle);
    };
}

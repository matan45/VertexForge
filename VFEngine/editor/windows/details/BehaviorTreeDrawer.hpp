#pragma once
#include "data/EntityHandle.hpp"

namespace windows::details
{
    class BehaviorTreeDrawer
    {
    public:
        bool draw(services::EntityHandle handle);
    };
}

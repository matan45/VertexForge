#pragma once
#include "data/EntityHandle.hpp"

namespace windows::details
{
    class OffMeshLinkDrawer
    {
    public:
        bool draw(services::EntityHandle handle);
    };
}

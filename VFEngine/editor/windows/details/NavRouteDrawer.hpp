#pragma once
#include "data/EntityHandle.hpp"

namespace windows::details
{
    // VK-1660: a patrol route (NavRouteComponent). Its ordered children are the markers FollowRoute walks.
    class NavRouteDrawer
    {
    public:
        bool draw(services::EntityHandle handle);
    };
}

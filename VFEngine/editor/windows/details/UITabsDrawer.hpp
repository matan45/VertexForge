#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    class UITabsDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawTabBarPosition(services::UITabsData& data);
        bool drawActiveTabIndex(services::UITabsData& data);
        void drawCurrentState(const services::UITabsData& data);
    };
}

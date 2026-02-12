#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    class UILayoutGroupDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawDirection(services::UILayoutGroupData& data);
        bool drawSpacing(services::UILayoutGroupData& data);
        bool drawPadding(services::UILayoutGroupData& data);
        bool drawChildAlignment(services::UILayoutGroupData& data);
    };
}

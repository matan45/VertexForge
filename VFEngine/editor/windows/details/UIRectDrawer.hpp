#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    class UIRectDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawAnchors(services::UIRectData& data);
        bool drawPivot(services::UIRectData& data);
        bool drawSizeDelta(services::UIRectData& data);
        bool drawAnchoredPosition(services::UIRectData& data);
    };
}

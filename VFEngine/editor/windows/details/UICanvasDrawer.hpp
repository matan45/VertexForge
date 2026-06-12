#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    class UICanvasDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawReferenceResolution(services::UICanvasData& data);
        bool drawScaleMode(services::UICanvasData& data);
        bool drawPixelsPerUnit(services::UICanvasData& data);
        bool drawSortOrder(services::UICanvasData& data);
        void drawTheme(services::EntityHandle handle);
    };
}

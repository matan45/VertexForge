#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    class UILabelDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawText(services::UILabelData& data);
        bool drawFontPath(services::UILabelData& data);
        bool drawFontSize(services::UILabelData& data);
        bool drawFontStyle(services::UILabelData& data);
        bool drawColor(services::UILabelData& data);
        bool drawAlignment(services::UILabelData& data);
        bool drawOverflow(services::UILabelData& data);
        bool drawSpacing(services::UILabelData& data);
    };
}

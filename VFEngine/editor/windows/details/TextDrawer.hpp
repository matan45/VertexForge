#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    class TextDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawFontPath(services::TextData& data);
        bool drawTextInput(services::TextData& data);
        bool drawFontSize(services::TextData& data);
        bool drawFontStyle(services::TextData& data);
        bool drawColor(services::TextData& data);
        bool drawLineSpacing(services::TextData& data);
        bool drawLetterSpacing(services::TextData& data);
        // VK-1637. Mirrors UILabelDrawer's equivalents; drawTextBox is the former
        // drawMaxWidth, widened to the whole layout box (width, height, wrap).
        bool drawAlignment(services::TextData& data);
        bool drawOverflow(services::TextData& data);
        bool drawTextBox(services::TextData& data);

        char textBuffer[1024] = {};
    };
}

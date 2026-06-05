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
        bool drawMaxWidth(services::TextData& data);

        char textBuffer[1024] = {};
    };
}

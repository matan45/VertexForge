#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    class UITextInputDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawTextFields(services::UITextInputData& data);
        bool drawFontSettings(services::UITextInputData& data);
        bool drawStateColors(services::UITextInputData& data);
        bool drawCaretSettings(services::UITextInputData& data);
        bool drawSelectionColor(services::UITextInputData& data);
        bool drawTransitionDuration(services::UITextInputData& data);
        bool drawInteractable(services::UITextInputData& data);
        bool drawMaxLength(services::UITextInputData& data);
        void drawCurrentState(const services::UITextInputData& data);
    };
}

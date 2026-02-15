#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    class UIDropdownDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawInteractable(services::UIDropdownData& data);
        bool drawOptionsList(services::UIDropdownData& data);
        bool drawSelectedIndex(services::UIDropdownData& data);
        bool drawPlaceholderText(services::UIDropdownData& data);
        bool drawMaxVisibleItems(services::UIDropdownData& data);
        bool drawHeaderColors(services::UIDropdownData& data);
        bool drawListColors(services::UIDropdownData& data);
        bool drawFontSettings(services::UIDropdownData& data);
        bool drawTransitionDuration(services::UIDropdownData& data);
        void drawCurrentState(const services::UIDropdownData& data);
    };
}

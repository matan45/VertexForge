#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    class UICheckboxDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawCheckedState(services::UICheckboxData& data);
        bool drawGroupName(services::UICheckboxData& data);
        bool drawAllowUncheck(services::UICheckboxData& data);
        bool drawStateColors(services::UICheckboxData& data);
        bool drawStateTextures(services::UICheckboxData& data);
        bool drawTransitionDuration(services::UICheckboxData& data);
        bool drawInteractable(services::UICheckboxData& data);
        bool drawLabelToggle(services::UICheckboxData& data);
        void drawCurrentState(const services::UICheckboxData& data);
    };
}

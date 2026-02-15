#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    class UISliderDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawValueConfig(services::UISliderData& data);
        bool drawOrientation(services::UISliderData& data);
        bool drawHandleAppearance(services::UISliderData& data);
        bool drawFillAppearance(services::UISliderData& data);
        bool drawConfig(services::UISliderData& data);
        void drawCurrentState(const services::UISliderData& data);
    };
}

#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    class UIProgressBarDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawValueConfig(services::UIProgressBarData& data);
        bool drawOrientation(services::UIProgressBarData& data);
        bool drawTrackAppearance(services::UIProgressBarData& data);
        bool drawFillAppearance(services::UIProgressBarData& data);
        bool drawInterpolation(services::UIProgressBarData& data);
        void drawCurrentState(const services::UIProgressBarData& data);
    };
}

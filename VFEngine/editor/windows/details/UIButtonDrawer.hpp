#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    class UIButtonDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawStateColors(services::UIButtonData& data);
        bool drawStateTextures(services::UIButtonData& data);
        bool drawTransitionDuration(services::UIButtonData& data);
        bool drawInteractable(services::UIButtonData& data);
        void drawCurrentState(const services::UIButtonData& data);
    };
}

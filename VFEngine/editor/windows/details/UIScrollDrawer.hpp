#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    class UIScrollDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawScrollEnabled(services::UIScrollData& data);
        bool drawScrollbarVisibility(services::UIScrollData& data);
        bool drawSensitivity(services::UIScrollData& data);
        void drawDebugInfo(services::EntityHandle handle);
    };
}

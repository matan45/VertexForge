#pragma once
#include "RenderTexturePickerWidget.hpp"
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    class UIImageDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawTexturePath(services::UIImageData& data);
        bool drawRenderTextureSource(services::UIImageData& data);
        bool drawColorTint(services::UIImageData& data);

        RenderTexturePickerWidget rttPicker;
    };
}

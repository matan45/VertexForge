#pragma once
#include "RenderTexturePickerWidget.hpp"
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    class UIImageDrawer
    {
    private:
        RenderTexturePickerWidget rttPicker;
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawTexturePath(services::UIImageData& data);
        bool drawRenderTextureSource(services::UIImageData& data);
        bool drawColorTint(services::UIImageData& data);
    };
}

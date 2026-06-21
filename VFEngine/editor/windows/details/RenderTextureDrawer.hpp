#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"
#include "RenderTexturePickerWidget.hpp"

namespace windows::details
{
    class RenderTextureDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawResolution(services::RenderTextureData& data);
        bool drawSettings(services::RenderTextureData& data);

        RenderTexturePickerWidget sourceCameraPicker;
    };
}

#pragma once
#include "RenderTexturePickerWidget.hpp"
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    class BillboardDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawTexturePath(services::BillboardData& data);
        bool drawRenderTextureSource(services::BillboardData& data);
        bool drawSizeInput(services::BillboardData& data);
        bool drawColorTint(services::BillboardData& data);
        bool drawDistanceSettings(services::BillboardData& data);
        void drawBakeImpostor(services::EntityHandle handle);

        RenderTexturePickerWidget rttPicker;
    };
}

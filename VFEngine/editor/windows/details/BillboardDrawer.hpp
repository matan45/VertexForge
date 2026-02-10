#pragma once
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
        bool drawSizeSettings(services::BillboardData& data);
        bool drawColorTint(services::BillboardData& data);
    };
}

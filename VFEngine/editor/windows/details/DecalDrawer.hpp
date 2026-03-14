#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    class DecalDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawHalfExtents(services::DecalData& data);
        bool drawTextures(services::DecalData& data);
        bool drawColor(services::DecalData& data);
        bool drawFadeSettings(services::DecalData& data);
        bool drawAdvancedSettings(services::DecalData& data);
    };
}

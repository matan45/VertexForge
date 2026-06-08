#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"
#include "asset/AssetRef.hpp"

namespace windows::details
{
    class DecalDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawShape(services::DecalData& data);
        bool drawHalfExtents(services::DecalData& data);
        bool drawTextures(services::DecalData& data);
        bool drawColor(services::DecalData& data);
        bool drawFadeSettings(services::DecalData& data);
        bool drawAdvancedSettings(services::DecalData& data);
        bool drawTextureSlot(const char* label, const char* emptyText, asset::AssetRef& textureRef);
    };
}

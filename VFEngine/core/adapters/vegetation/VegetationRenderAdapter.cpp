#include "VegetationRenderAdapter.hpp"

namespace core::adapters
{
    void VegetationRenderAdapter::setVegetationRenderingEnabled(bool enabled)
    {
        renderingEnabled = enabled;
    }

    bool VegetationRenderAdapter::isVegetationRenderingEnabled() const
    {
        return renderingEnabled;
    }

    void VegetationRenderAdapter::markTileDirty(int32_t coordX, int32_t coordZ)
    {
        if (markDirtyCallback) markDirtyCallback(coordX, coordZ);
    }

    void VegetationRenderAdapter::addTile(int32_t coordX, int32_t coordZ)
    {
        if (addTileCallback) addTileCallback(coordX, coordZ);
    }

    void VegetationRenderAdapter::removeTile(int32_t coordX, int32_t coordZ)
    {
        if (removeTileCallback) removeTileCallback(coordX, coordZ);
    }
}

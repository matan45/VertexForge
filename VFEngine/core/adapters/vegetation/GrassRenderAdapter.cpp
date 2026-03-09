#include "GrassRenderAdapter.hpp"

namespace core::adapters
{
    void GrassRenderAdapter::setGrassRenderingEnabled(bool enabled)
    {
        renderingEnabled = enabled;
    }

    bool GrassRenderAdapter::isGrassRenderingEnabled() const
    {
        return renderingEnabled;
    }

    void GrassRenderAdapter::markTileDirty(int32_t coordX, int32_t coordZ)
    {
        if (markDirtyCallback) markDirtyCallback(coordX, coordZ);
    }

    void GrassRenderAdapter::addTile(int32_t coordX, int32_t coordZ)
    {
        if (addTileCallback) addTileCallback(coordX, coordZ);
    }

    void GrassRenderAdapter::removeTile(int32_t coordX, int32_t coordZ)
    {
        if (removeTileCallback) removeTileCallback(coordX, coordZ);
    }

    vegetation::GrassRenderConfig GrassRenderAdapter::getGrassRenderConfig() const
    {
        if (getConfigCallback)
        {
            return getConfigCallback();
        }
        return {};
    }
}

#include "GrassRenderAdapter.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/VegetationComponents.hpp"

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
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::GrassComponent>();
        for (auto entity : view)
        {
            const auto& comp = view.get<components::GrassComponent>(entity);
            return comp.config;
        }
        return {};
    }
}

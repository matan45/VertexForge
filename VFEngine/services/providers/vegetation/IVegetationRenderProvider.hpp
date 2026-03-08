#pragma once

#include <cstdint>
#include <functional>
#include <glm/glm.hpp>

namespace services
{
    class IVegetationRenderProvider
    {
    public:
        using TileCallback = std::function<void(int32_t, int32_t)>;

        virtual ~IVegetationRenderProvider() = default;

        virtual void setVegetationRenderingEnabled(bool enabled) = 0;
        virtual bool isVegetationRenderingEnabled() const = 0;

        virtual void markTileDirty(int32_t coordX, int32_t coordZ) = 0;
        virtual void addTile(int32_t coordX, int32_t coordZ) = 0;
        virtual void removeTile(int32_t coordX, int32_t coordZ) = 0;

        // Callbacks for wiring to renderer (set by RenderPassHandler)
        virtual void setAddTileCallback(TileCallback) {}
        virtual void setRemoveTileCallback(TileCallback) {}
        virtual void setMarkDirtyCallback(TileCallback) {}
    };
}

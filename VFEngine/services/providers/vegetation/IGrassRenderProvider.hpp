#pragma once

#include <cstdint>
#include <functional>
#include <vector>
#include <glm/glm.hpp>
#include "vegetation/GrassConfig.hpp"
#include "vegetation/VegetationTypes.hpp"

namespace services
{
    class IGrassRenderProvider
    {
    public:
        using TileCallback = std::function<void(int32_t, int32_t)>;
        using GetConfigCallback = std::function<vegetation::GrassRenderConfig()>;
        using BillboardPaletteCallback = std::function<void(const std::vector<vegetation::BillboardPaletteEntry>&)>;

        virtual ~IGrassRenderProvider() = default;

        virtual void setGrassRenderingEnabled(bool enabled) = 0;
        virtual bool isGrassRenderingEnabled() const = 0;

        virtual void markTileDirty(int32_t coordX, int32_t coordZ) = 0;
        virtual void addTile(int32_t coordX, int32_t coordZ) = 0;
        virtual void removeTile(int32_t coordX, int32_t coordZ) = 0;

        virtual vegetation::GrassRenderConfig getGrassRenderConfig() const = 0;

        // Callbacks for wiring to renderer (set by RenderPassHandler)
        virtual void setAddTileCallback(TileCallback) {}
        virtual void setRemoveTileCallback(TileCallback) {}
        virtual void setMarkDirtyCallback(TileCallback) {}

        // Callback for config retrieval (set by service layer to avoid direct ECS access)
        virtual void setGetConfigCallback(GetConfigCallback) {}

        // Billboard palette update
        virtual void setBillboardPalette(const std::vector<vegetation::BillboardPaletteEntry>&) {}
        virtual void setOnBillboardPaletteChanged(BillboardPaletteCallback) {}
    };
}

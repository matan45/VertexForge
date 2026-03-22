#pragma once

#include "../../../services/providers/vegetation/IGrassRenderProvider.hpp"

namespace core::adapters
{
    class GrassRenderAdapter : public services::IGrassRenderProvider
    {
    private:
        bool renderingEnabled = true;
        TileCallback addTileCallback;
        TileCallback removeTileCallback;
        TileCallback markDirtyCallback;
        GetConfigCallback getConfigCallback;
        BillboardPaletteCallback billboardPaletteCallback;
        
    public:
        GrassRenderAdapter() = default;
        ~GrassRenderAdapter() override = default;

        void setGrassRenderingEnabled(bool enabled) override;
        bool isGrassRenderingEnabled() const override;

        void markTileDirty(int32_t coordX, int32_t coordZ) override;
        void addTile(int32_t coordX, int32_t coordZ) override;
        void removeTile(int32_t coordX, int32_t coordZ) override;

        vegetation::GrassRenderConfig getGrassRenderConfig() const override;

        void setAddTileCallback(TileCallback cb) override { addTileCallback = std::move(cb); }
        void setRemoveTileCallback(TileCallback cb) override { removeTileCallback = std::move(cb); }
        void setMarkDirtyCallback(TileCallback cb) override { markDirtyCallback = std::move(cb); }
        void setGetConfigCallback(GetConfigCallback cb) override { getConfigCallback = std::move(cb); }
        void setBillboardPalette(const std::vector<vegetation::BillboardPaletteEntry>& entries, int32_t activeEntry = -1) override
        {
            if (billboardPaletteCallback) billboardPaletteCallback(entries, activeEntry);
        }
        void setOnBillboardPaletteChanged(BillboardPaletteCallback cb) override { billboardPaletteCallback = std::move(cb); }
    };
}

#pragma once

#include "../../../services/providers/vegetation/IVegetationRenderProvider.hpp"

namespace core::adapters
{
    class VegetationRenderAdapter : public services::IVegetationRenderProvider
    {
    public:
        VegetationRenderAdapter() = default;
        ~VegetationRenderAdapter() override = default;

        void setVegetationRenderingEnabled(bool enabled) override;
        bool isVegetationRenderingEnabled() const override;

        void markTileDirty(int32_t coordX, int32_t coordZ) override;
        void addTile(int32_t coordX, int32_t coordZ) override;
        void removeTile(int32_t coordX, int32_t coordZ) override;

        void setAddTileCallback(TileCallback cb) override { addTileCallback = std::move(cb); }
        void setRemoveTileCallback(TileCallback cb) override { removeTileCallback = std::move(cb); }
        void setMarkDirtyCallback(TileCallback cb) override { markDirtyCallback = std::move(cb); }

    private:
        bool renderingEnabled = true;
        TileCallback addTileCallback;
        TileCallback removeTileCallback;
        TileCallback markDirtyCallback;
    };
}

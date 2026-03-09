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

        void updateSpecies(uint32_t speciesId, const vegetation::VegetationSpeciesConfig& config) override;
        void removeSpecies(uint32_t speciesId) override;
        void clearAllSpecies() override;

        void setDebugLODView(bool enabled) override;
        bool isDebugLODView() const override;

        void setAddTileCallback(TileCallback cb) override { addTileCallback = std::move(cb); }
        void setRemoveTileCallback(TileCallback cb) override { removeTileCallback = std::move(cb); }
        void setMarkDirtyCallback(TileCallback cb) override { markDirtyCallback = std::move(cb); }
        void setUpdateSpeciesCallback(SpeciesUpdateCallback cb) override { updateSpeciesCallback = std::move(cb); }
        void setRemoveSpeciesCallback(SpeciesRemoveCallback cb) override { removeSpeciesCallback = std::move(cb); }
        void setClearAllSpeciesCallback(std::function<void()> cb) override { clearAllSpeciesCallback = std::move(cb); }
        void setDebugLODViewCallback(std::function<void(bool)> cb) override { debugLODViewCallback = std::move(cb); }

    private:
        bool renderingEnabled = true;
        bool debugLODViewEnabled = false;
        TileCallback addTileCallback;
        TileCallback removeTileCallback;
        TileCallback markDirtyCallback;
        SpeciesUpdateCallback updateSpeciesCallback;
        SpeciesRemoveCallback removeSpeciesCallback;
        std::function<void()> clearAllSpeciesCallback;
        std::function<void(bool)> debugLODViewCallback;
    };
}

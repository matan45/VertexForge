#pragma once

#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include "../../../utilities/vegetation/VegetationSpecies.hpp"

namespace services
{
    class IVegetationRenderProvider
    {
    public:
        using TileCallback = std::function<void(int32_t, int32_t)>;
        using SpeciesUpdateCallback = std::function<void(uint32_t, const vegetation::VegetationSpeciesConfig&)>;
        using SpeciesRemoveCallback = std::function<void(uint32_t)>;

        virtual ~IVegetationRenderProvider() = default;

        virtual void setVegetationRenderingEnabled(bool enabled) = 0;
        virtual bool isVegetationRenderingEnabled() const = 0;

        virtual void markTileDirty(int32_t coordX, int32_t coordZ) = 0;
        virtual void addTile(int32_t coordX, int32_t coordZ) = 0;
        virtual void removeTile(int32_t coordX, int32_t coordZ) = 0;

        virtual void updateSpecies(uint32_t speciesId, const vegetation::VegetationSpeciesConfig& config) = 0;
        virtual void removeSpecies(uint32_t speciesId) = 0;
        virtual void clearAllSpecies() = 0;

        virtual void setDebugLODView(bool enabled) = 0;
        virtual bool isDebugLODView() const = 0;

        // Callbacks for wiring to renderer (set by RenderPassHandler)
        virtual void setAddTileCallback(TileCallback) {}
        virtual void setRemoveTileCallback(TileCallback) {}
        virtual void setMarkDirtyCallback(TileCallback) {}
        virtual void setUpdateSpeciesCallback(SpeciesUpdateCallback) {}
        virtual void setRemoveSpeciesCallback(SpeciesRemoveCallback) {}
        virtual void setClearAllSpeciesCallback(std::function<void()>) {}
        virtual void setDebugLODViewCallback(std::function<void(bool)>) {}
    };
}

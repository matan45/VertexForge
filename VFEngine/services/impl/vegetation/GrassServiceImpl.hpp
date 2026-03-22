#pragma once
#include "../../interfaces/vegetation/IGrassService.hpp"
#include "../../data/EntityHandle.hpp"
#include "vegetation/GrassConfig.hpp"
#include "vegetation/VegetationTypes.hpp"
#include <functional>
#include <vector>

namespace services
{
    using BillboardPaletteCallback = std::function<void(const std::vector<vegetation::BillboardPaletteEntry>&, int32_t activeEntry)>;

    class GrassServiceImpl : public IGrassService
    {
    public:
        GrassServiceImpl() = default;
        ~GrassServiceImpl() override;

        void registerEventHandlers() override;
        void setBillboardPaletteCallback(BillboardPaletteCallback cb) { billboardPaletteCb = std::move(cb); }

    private:
        void setGrassConfig(EntityHandle entityId, const vegetation::GrassRenderConfig& config);
        vegetation::GrassRenderConfig getGrassConfig(EntityHandle entityId) const;
        void setGlobalGrassConfig(const vegetation::GrassRenderConfig& config);
        vegetation::GrassRenderConfig getGlobalGrassConfig() const;

        BillboardPaletteCallback billboardPaletteCb;
    };
}

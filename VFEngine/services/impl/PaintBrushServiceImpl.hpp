#pragma once
#include "../interfaces/IPaintBrushService.hpp"
#include "../events/EventTypes.hpp"

namespace services
{
    class PaintBrushServiceImpl : public IPaintBrushService
    {
    private:
        terrain::PaintBrushParams currentParams;
        terrain::PaintBrushType currentBrushType = terrain::PaintBrushType::PaintLayer;
        bool paintModeActive = false;

        ::events::SubscriptionToken paintModeToken;

    public:
        PaintBrushServiceImpl() = default;
        ~PaintBrushServiceImpl() override;

        void registerEventHandlers() override;

        void setParams(const terrain::PaintBrushParams& params) override;
        void setRadius(float radius) override;
        void setStrength(float strength) override;
        void setOpacity(float opacity) override;
        void setActiveLayer(uint32_t layer) override;
        terrain::PaintBrushParams getParams() const override;

        void setBrushType(terrain::PaintBrushType type) override;
        terrain::PaintBrushType getBrushType() const override;

    private:
        void publishParamsChanged();
        void publishTypeChanged();
    };
}

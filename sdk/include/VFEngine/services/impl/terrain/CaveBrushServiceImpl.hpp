#pragma once
#include "../../interfaces/terrain/ICaveBrushService.hpp"
#include "../../events/EventTypes.hpp"

namespace services
{
    class CaveBrushServiceImpl : public ICaveBrushService
    {
    private:
        terrain::CaveBrushParams currentParams;
        terrain::CaveBrushType currentBrushType = terrain::CaveBrushType::Carve;
        bool caveModeActive = false;

        ::events::SubscriptionToken caveModeToken;

    public:
        CaveBrushServiceImpl() = default;
        ~CaveBrushServiceImpl() override;

        void registerEventHandlers() override;

        void setParams(const terrain::CaveBrushParams& params) override;
        void setRadius(float radius) override;
        void setStrength(float strength) override;
        void setBrushType(terrain::CaveBrushType type) override;
        terrain::CaveBrushParams getParams() const override;
        terrain::CaveBrushType getBrushType() const override;

    private:
        void publishParamsChanged();
        void publishTypeChanged();
    };
}

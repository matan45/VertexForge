#pragma once
#include "../../interfaces/terrain/IHoleBrushService.hpp"
#include "../../events/EventTypes.hpp"

namespace services
{
    class HoleBrushServiceImpl : public IHoleBrushService
    {
    private:
        terrain::HoleBrushParams currentParams;
        bool holeModeActive = false;

        ::events::SubscriptionToken holeModeToken;

    public:
        HoleBrushServiceImpl() = default;
        ~HoleBrushServiceImpl() override;

        void registerEventHandlers() override;

        void setParams(const terrain::HoleBrushParams& params) override;
        void setRadius(float radius) override;
        terrain::HoleBrushParams getParams() const override;

    private:
        void publishParamsChanged();
    };
}

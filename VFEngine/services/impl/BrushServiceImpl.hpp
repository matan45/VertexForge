#pragma once
#include "../interfaces/IBrushService.hpp"
#include "../events/EventTypes.hpp"

namespace services
{
    class BrushServiceImpl : public IBrushService
    {
    private:
        terrain::BrushParams currentParams;
        bool sculptModeActive = false;

        ::events::SubscriptionToken sculptModeToken;

    public:
        BrushServiceImpl() = default;
        ~BrushServiceImpl() override;

        void registerEventHandlers() override;

        void setParams(const terrain::BrushParams& params) override;
        void setRadius(float radius) override;
        void setStrength(float strength) override;
        terrain::BrushParams getParams() const override;

    private:
        void publishParamsChanged();
    };
}

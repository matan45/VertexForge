#pragma once
#include "../../interfaces/vegetation/IVegetationBrushService.hpp"
#include "../../events/EventTypes.hpp"
#include "../../../utilities/vegetation/VegetationTypes.hpp"

namespace services
{
    class VegetationBrushServiceImpl : public IVegetationBrushService
    {
    private:
        vegetation::DensityBrushParams currentDensityParams;
        vegetation::DensityBrushType currentDensityBrushType = vegetation::DensityBrushType::Paint;
        vegetation::VegetationType activeVegetationType = vegetation::VegetationType::Billboard;
        vegetation::MixedBrushConfig mixedBrushConfig;
        bool vegetationModeActive = false;

        ::events::SubscriptionToken vegetationModeToken;

    public:
        VegetationBrushServiceImpl() = default;
        ~VegetationBrushServiceImpl() override;

        void registerEventHandlers() override;

    private:
        void setDensityParams(const vegetation::DensityBrushParams& params);
        void setDensityBrushType(vegetation::DensityBrushType type);

        vegetation::DensityBrushParams getDensityParams() const;
        vegetation::DensityBrushType getDensityBrushType() const;

        void publishDensityParamsChanged();
        void publishDensityTypeChanged();
    };
}

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
        vegetation::PlacementBrushParams currentPlacementParams;
        vegetation::DensityBrushType currentDensityBrushType = vegetation::DensityBrushType::Paint;
        vegetation::PlacementBrushType currentPlacementBrushType = vegetation::PlacementBrushType::Scatter;
        bool vegetationModeActive = false;
        bool placementModeActive = false;

        ::events::SubscriptionToken vegetationModeToken;
        ::events::SubscriptionToken placementModeToken;

    public:
        VegetationBrushServiceImpl() = default;
        ~VegetationBrushServiceImpl() override;

        void registerEventHandlers() override;

    private:
        void setDensityParams(const vegetation::DensityBrushParams& params);
        void setPlacementParams(const vegetation::PlacementBrushParams& params);
        void setDensityBrushType(vegetation::DensityBrushType type);
        void setPlacementBrushType(vegetation::PlacementBrushType type);

        vegetation::DensityBrushParams getDensityParams() const;
        vegetation::PlacementBrushParams getPlacementParams() const;
        vegetation::DensityBrushType getDensityBrushType() const;
        vegetation::PlacementBrushType getPlacementBrushType() const;

        void publishDensityParamsChanged();
        void publishPlacementParamsChanged();
        void publishDensityTypeChanged();
        void publishPlacementTypeChanged();
    };
}

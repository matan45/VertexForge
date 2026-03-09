#pragma once
#include "../../interfaces/vegetation/IVegetationPlacementModeService.hpp"
#include "../../events/EventTypes.hpp"

namespace services
{
    class VegetationPlacementModeServiceImpl : public IVegetationPlacementModeService
    {
    private:
        bool placementActive = false;
        std::optional<EntityHandle> targetTerrain;

        ::events::SubscriptionToken editorModeToken;
        ::events::SubscriptionToken entityDeletedToken;
        ::events::SubscriptionToken sceneClearedToken;
        ::events::SubscriptionToken sculptModeToken;
        ::events::SubscriptionToken paintModeToken;
        ::events::SubscriptionToken holeModeToken;
        ::events::SubscriptionToken vegBrushModeToken;

    public:
        VegetationPlacementModeServiceImpl() = default;
        ~VegetationPlacementModeServiceImpl() override;

        void registerEventHandlers() override;

        bool activate() override;
        void deactivate() override;
        bool isActive() const override;

        std::optional<EntityHandle> getTargetEntity() const override;
    };
}

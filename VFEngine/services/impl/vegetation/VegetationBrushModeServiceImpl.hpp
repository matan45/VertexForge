#pragma once
#include "../../interfaces/vegetation/IVegetationBrushModeService.hpp"
#include "../../events/EventTypes.hpp"

namespace services
{
    class VegetationBrushModeServiceImpl : public IVegetationBrushModeService
    {
    private:
        bool vegetationBrushActive = false;
        std::optional<EntityHandle> targetTerrain;

        ::events::SubscriptionToken editorModeToken;
        ::events::SubscriptionToken entityDeletedToken;
        ::events::SubscriptionToken sceneClearedToken;
        ::events::SubscriptionToken sculptModeToken;
        ::events::SubscriptionToken paintModeToken;
        ::events::SubscriptionToken holeModeToken;
        ::events::SubscriptionToken meshBrushModeToken;
        ::events::SubscriptionToken foliageBrushModeToken;

    public:
        VegetationBrushModeServiceImpl() = default;
        ~VegetationBrushModeServiceImpl() override;

        void registerEventHandlers() override;

        bool activate() override;
        void deactivate() override;
        bool isActive() const override;

        std::optional<EntityHandle> getTargetEntity() const override;
    };
}

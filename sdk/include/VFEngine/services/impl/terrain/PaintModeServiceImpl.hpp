#pragma once
#include "../../interfaces/terrain/IPaintModeService.hpp"
#include "../../events/EventTypes.hpp"

namespace services
{
    class PaintModeServiceImpl : public IPaintModeService
    {
    private:
        bool paintActive = false;
        std::optional<EntityHandle> targetTerrain;

        ::events::SubscriptionToken editorModeToken;
        ::events::SubscriptionToken entityDeletedToken;
        ::events::SubscriptionToken sceneClearedToken;
        ::events::SubscriptionToken sculptModeToken;
        ::events::SubscriptionToken holeModeToken;
        ::events::SubscriptionToken caveModeToken;
        ::events::SubscriptionToken vegetationBrushModeToken;
        ::events::SubscriptionToken foliageBrushModeToken;

    public:
        PaintModeServiceImpl() = default;
        ~PaintModeServiceImpl() override;

        void registerEventHandlers() override;

        bool activate() override;
        void deactivate() override;
        bool isActive() const override;

        std::optional<EntityHandle> getTargetEntity() const override;
    };
}

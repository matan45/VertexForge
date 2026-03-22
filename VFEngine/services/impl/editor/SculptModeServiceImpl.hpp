#pragma once
#include "../../interfaces/editor/ISculptModeService.hpp"
#include "../../events/EventTypes.hpp"

namespace services
{
    class SculptModeServiceImpl : public ISculptModeService
    {
    private:
        bool sculptActive = false;
        std::optional<EntityHandle> targetTerrain;

        ::events::SubscriptionToken editorModeToken;
        ::events::SubscriptionToken entityDeletedToken;
        ::events::SubscriptionToken sceneClearedToken;
        ::events::SubscriptionToken paintModeToken;
        ::events::SubscriptionToken holeModeToken;
        ::events::SubscriptionToken caveModeToken;
        ::events::SubscriptionToken vegetationBrushModeToken;

    public:
        SculptModeServiceImpl() = default;
        ~SculptModeServiceImpl() override;

        void registerEventHandlers() override;

        bool activate() override;
        void deactivate() override;
        bool isActive() const override;

        std::optional<EntityHandle> getTargetEntity() const override;
    };
}

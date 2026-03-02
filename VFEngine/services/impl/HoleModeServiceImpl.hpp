#pragma once
#include "../interfaces/IHoleModeService.hpp"
#include "../events/EventTypes.hpp"

namespace services
{
    class HoleModeServiceImpl : public IHoleModeService
    {
    private:
        bool holeActive = false;
        std::optional<EntityHandle> targetTerrain;

        ::events::SubscriptionToken editorModeToken;
        ::events::SubscriptionToken entityDeletedToken;
        ::events::SubscriptionToken sceneClearedToken;
        ::events::SubscriptionToken sculptModeToken;
        ::events::SubscriptionToken paintModeToken;

    public:
        HoleModeServiceImpl() = default;
        ~HoleModeServiceImpl() override;

        void registerEventHandlers() override;

        bool activate() override;
        void deactivate() override;
        bool isActive() const override;

        std::optional<EntityHandle> getTargetEntity() const override;
    };
}

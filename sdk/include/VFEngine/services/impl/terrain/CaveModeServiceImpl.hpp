#pragma once
#include "../../interfaces/terrain/ICaveModeService.hpp"
#include "../../events/EventTypes.hpp"

namespace events { class EventDispatcher; }

namespace services
{
    class CaveModeServiceImpl : public ICaveModeService
    {
    private:
        bool caveActive = false;
        std::optional<EntityHandle> targetTerrain;

        ::events::SubscriptionToken editorModeToken;
        ::events::SubscriptionToken entityDeletedToken;
        ::events::SubscriptionToken sceneClearedToken;
        ::events::SubscriptionToken sculptModeToken;
        ::events::SubscriptionToken paintModeToken;
        ::events::SubscriptionToken holeModeToken;
        ::events::SubscriptionToken vegetationBrushModeToken;
        ::events::SubscriptionToken meshBrushModeToken;

    public:
        CaveModeServiceImpl() = default;
        ~CaveModeServiceImpl() override;

        void registerEventHandlers() override;

        bool activate() override;
        void deactivate() override;
        bool isActive() const override;

        std::optional<EntityHandle> getTargetEntity() const override;

    private:
        void subscribeToModeChanges(::events::EventDispatcher& dispatcher);
        std::optional<EntityHandle> resolveTerrainEntity(::events::EventDispatcher& dispatcher);
    };
}

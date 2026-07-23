#pragma once
#include "../../interfaces/terrain/ITerrainRaycastService.hpp"
#include "../../events/EventTypes.hpp"

namespace services
{
    class ITerrainRaycastProvider;

    class TerrainRaycastServiceImpl : public ITerrainRaycastService
    {
    private:
        ITerrainRaycastProvider* provider;
        bool sculptModeActive = false;
        bool paintModeActive = false;
        bool holeModeActive = false;
        bool caveModeActive = false;
        bool vegBrushModeActive = false;
        bool meshBrushModeActive = false;
        bool foliageBrushModeActive = false;
        bool splineModeActive = false;
        ::events::SubscriptionToken sculptModeToken;
        ::events::SubscriptionToken paintModeToken;
        ::events::SubscriptionToken holeModeToken;
        ::events::SubscriptionToken caveModeToken;
        ::events::SubscriptionToken caveBrushParamsToken;
        ::events::SubscriptionToken brushParamsToken;
        ::events::SubscriptionToken paintBrushParamsToken;
        ::events::SubscriptionToken holeBrushParamsToken;
        ::events::SubscriptionToken vegBrushModeToken;
        ::events::SubscriptionToken vegBrushParamsToken;
        ::events::SubscriptionToken meshBrushModeToken;
        ::events::SubscriptionToken meshBrushParamsToken;
        ::events::SubscriptionToken foliageBrushModeToken;
        ::events::SubscriptionToken foliageBrushParamsToken;
        ::events::SubscriptionToken splineModeToken;

    public:
        explicit TerrainRaycastServiceImpl(ITerrainRaycastProvider* provider);
        ~TerrainRaycastServiceImpl() override;

        void registerEventHandlers() override;

    private:
        void unsubscribeAll();
    };
}

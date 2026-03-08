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
        bool vegBrushModeActive = false;
        bool vegPlacementModeActive = false;

        ::events::SubscriptionToken sculptModeToken;
        ::events::SubscriptionToken paintModeToken;
        ::events::SubscriptionToken holeModeToken;
        ::events::SubscriptionToken brushParamsToken;
        ::events::SubscriptionToken paintBrushParamsToken;
        ::events::SubscriptionToken holeBrushParamsToken;
        ::events::SubscriptionToken vegBrushModeToken;
        ::events::SubscriptionToken vegBrushParamsToken;
        ::events::SubscriptionToken vegPlacementModeToken;
        ::events::SubscriptionToken vegPlacementParamsToken;

    public:
        explicit TerrainRaycastServiceImpl(ITerrainRaycastProvider* provider);
        ~TerrainRaycastServiceImpl() override;

        void registerEventHandlers() override;
    };
}

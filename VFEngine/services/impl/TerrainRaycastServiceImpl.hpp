#pragma once
#include "../interfaces/ITerrainRaycastService.hpp"
#include "../events/EventTypes.hpp"

namespace services
{
    class ITerrainRaycastProvider;

    class TerrainRaycastServiceImpl : public ITerrainRaycastService
    {
    private:
        ITerrainRaycastProvider* provider;
        bool sculptModeActive = false;
        bool paintModeActive = false;

        ::events::SubscriptionToken sculptModeToken;
        ::events::SubscriptionToken paintModeToken;
        ::events::SubscriptionToken brushParamsToken;
        ::events::SubscriptionToken paintBrushParamsToken;

    public:
        explicit TerrainRaycastServiceImpl(ITerrainRaycastProvider* provider);
        ~TerrainRaycastServiceImpl() override;

        void registerEventHandlers() override;
    };
}

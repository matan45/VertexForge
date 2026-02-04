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

        ::events::SubscriptionToken sculptModeToken;

    public:
        explicit TerrainRaycastServiceImpl(ITerrainRaycastProvider* provider);
        ~TerrainRaycastServiceImpl() override;

        void registerEventHandlers() override;
    };
}

#include "TerrainRaycastServiceImpl.hpp"
#include "../providers/ITerrainRaycastProvider.hpp"
#include "../events/EventDispatcher.hpp"
#include "../events/TerrainRaycastEvents.hpp"
#include "../events/SculptModeEvents.hpp"

namespace services
{
    TerrainRaycastServiceImpl::TerrainRaycastServiceImpl(ITerrainRaycastProvider* provider)
        : provider(provider)
    {
    }

    TerrainRaycastServiceImpl::~TerrainRaycastServiceImpl()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (sculptModeToken.isValid())
        {
            dispatcher.unsubscribe(sculptModeToken);
        }
    }

    void TerrainRaycastServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::terrainRaycast::SetCursorPositionCommand>(
            [this](const events::terrainRaycast::SetCursorPositionCommand& cmd)
            {
                if (!sculptModeActive || !provider)
                {
                    return;
                }

                provider->setRaycastCursorUV(cmd.cursorUV);
            });

        dispatcher.registerCommandHandler<events::terrainRaycast::ClearCursorCommand>(
            [this](const events::terrainRaycast::ClearCursorCommand&)
            {
                if (provider)
                {
                    provider->clearRaycastCursor();
                }
            });

        dispatcher.registerQueryHandler<events::terrainRaycast::GetTerrainHitQuery>(
            [this](const events::terrainRaycast::GetTerrainHitQuery&)
            {
                if (!provider)
                {
                    return terrain::TerrainHitResult{};
                }

                return provider->getTerrainHitResult();
            });

        sculptModeToken = dispatcher.subscribe<events::sculpt::SculptModeChangedNotification>(
            [this](const events::sculpt::SculptModeChangedNotification& n)
            {
                if (n.isActive)
                {
                    sculptModeActive = true;
                }
                else
                {
                    sculptModeActive = false;
                    if (provider)
                    {
                        provider->clearRaycastCursor();
                    }
                }
            });
    }
}

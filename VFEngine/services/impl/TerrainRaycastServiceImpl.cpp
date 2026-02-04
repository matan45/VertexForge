#include "TerrainRaycastServiceImpl.hpp"
#include "../providers/ITerrainRaycastProvider.hpp"
#include "../events/EventDispatcher.hpp"
#include "../events/TerrainRaycastEvents.hpp"
#include "../events/SculptModeEvents.hpp"
#include "../events/BrushEvents.hpp"

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

        if (brushParamsToken.isValid())
        {
            dispatcher.unsubscribe(brushParamsToken);
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
                    if (provider)
                    {
                        auto brushParams = events::EventDispatcher::instance().query(
                            events::brush::GetBrushParamsQuery{});
                        provider->setBrushOverlayParams(
                            brushParams.radius,
                            static_cast<float>(brushParams.falloff));
                    }
                }
                else
                {
                    sculptModeActive = false;
                    if (provider)
                    {
                        provider->clearRaycastCursor();
                        provider->setBrushOverlayParams(0.0f, 0.0f);
                    }
                }
            });

        brushParamsToken = dispatcher.subscribe<events::brush::BrushParamsChangedNotification>(
            [this](const events::brush::BrushParamsChangedNotification& n)
            {
                if (sculptModeActive && provider)
                {
                    provider->setBrushOverlayParams(
                        n.params.radius,
                        static_cast<float>(n.params.falloff));
                }
            });
    }
}

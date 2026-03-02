#include "TerrainRaycastServiceImpl.hpp"
#include "../providers/ITerrainRaycastProvider.hpp"
#include "../events/EventDispatcher.hpp"
#include "../events/TerrainRaycastEvents.hpp"
#include "../events/SculptModeEvents.hpp"
#include "../events/BrushEvents.hpp"
#include "../events/PaintModeEvents.hpp"
#include "../events/PaintBrushEvents.hpp"
#include "../events/HoleModeEvents.hpp"
#include "../events/HoleBrushEvents.hpp"

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

        if (paintModeToken.isValid())
        {
            dispatcher.unsubscribe(paintModeToken);
        }

        if (brushParamsToken.isValid())
        {
            dispatcher.unsubscribe(brushParamsToken);
        }

        if (paintBrushParamsToken.isValid())
        {
            dispatcher.unsubscribe(paintBrushParamsToken);
        }

        if (holeModeToken.isValid())
        {
            dispatcher.unsubscribe(holeModeToken);
        }

        if (holeBrushParamsToken.isValid())
        {
            dispatcher.unsubscribe(holeBrushParamsToken);
        }
    }

    void TerrainRaycastServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::terrainRaycast::SetCursorPositionCommand>(
            [this](const events::terrainRaycast::SetCursorPositionCommand& cmd)
            {
                if ((!sculptModeActive && !paintModeActive && !holeModeActive) || !provider)
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
                            static_cast<float>(brushParams.falloff),
                            static_cast<float>(brushParams.shape));
                    }
                }
                else
                {
                    sculptModeActive = false;
                    if (provider)
                    {
                        provider->clearRaycastCursor();
                        provider->setBrushOverlayParams(0.0f, 0.0f, 0.0f);
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
                        static_cast<float>(n.params.falloff),
                        static_cast<float>(n.params.shape));
                }
            });

        paintModeToken = dispatcher.subscribe<events::paint::PaintModeChangedNotification>(
            [this](const events::paint::PaintModeChangedNotification& n)
            {
                if (n.isActive)
                {
                    paintModeActive = true;
                    if (provider)
                    {
                        auto brushParams = events::EventDispatcher::instance().query(
                            events::paintBrush::GetPaintBrushParamsQuery{});
                        provider->setBrushOverlayParams(
                            brushParams.radius,
                            static_cast<float>(brushParams.falloff),
                            static_cast<float>(brushParams.shape));
                    }
                }
                else
                {
                    paintModeActive = false;
                    if (provider)
                    {
                        provider->clearRaycastCursor();
                        provider->setBrushOverlayParams(0.0f, 0.0f, 0.0f);
                    }
                }
            });

        paintBrushParamsToken = dispatcher.subscribe<events::paintBrush::PaintBrushParamsChangedNotification>(
            [this](const events::paintBrush::PaintBrushParamsChangedNotification& n)
            {
                if (paintModeActive && provider)
                {
                    provider->setBrushOverlayParams(
                        n.params.radius,
                        static_cast<float>(n.params.falloff),
                        static_cast<float>(n.params.shape));
                }
            });

        holeModeToken = dispatcher.subscribe<events::hole::HoleModeChangedNotification>(
            [this](const events::hole::HoleModeChangedNotification& n)
            {
                if (n.isActive)
                {
                    holeModeActive = true;
                    if (provider)
                    {
                        auto brushParams = events::EventDispatcher::instance().query(
                            events::holeBrush::GetHoleBrushParamsQuery{});
                        provider->setBrushOverlayParams(
                            brushParams.radius,
                            static_cast<float>(brushParams.falloff),
                            static_cast<float>(brushParams.shape));
                    }
                }
                else
                {
                    holeModeActive = false;
                    if (provider)
                    {
                        provider->clearRaycastCursor();
                        provider->setBrushOverlayParams(0.0f, 0.0f, 0.0f);
                    }
                }
            });

        holeBrushParamsToken = dispatcher.subscribe<events::holeBrush::HoleBrushParamsChangedNotification>(
            [this](const events::holeBrush::HoleBrushParamsChangedNotification& n)
            {
                if (holeModeActive && provider)
                {
                    provider->setBrushOverlayParams(
                        n.params.radius,
                        static_cast<float>(n.params.falloff),
                        static_cast<float>(n.params.shape));
                }
            });
    }
}

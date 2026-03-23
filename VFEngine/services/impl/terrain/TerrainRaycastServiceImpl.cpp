#include "TerrainRaycastServiceImpl.hpp"
#include "../../providers/terrain/ITerrainRaycastProvider.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/terrain/TerrainRaycastEvents.hpp"
#include "../../events/editor/SculptModeEvents.hpp"
#include "../../events/terrain/BrushEvents.hpp"
#include "../../events/terrain/PaintModeEvents.hpp"
#include "../../events/terrain/PaintBrushEvents.hpp"
#include "../../events/terrain/HoleModeEvents.hpp"
#include "../../events/terrain/HoleBrushEvents.hpp"
#include "../../events/terrain/CaveModeEvents.hpp"
#include "../../events/terrain/CaveBrushEvents.hpp"
#include "../../events/vegetation/VegetationBrushEvents.hpp"
#include "../../events/meshbrush/MeshBrushEvents.hpp"

namespace services
{
    TerrainRaycastServiceImpl::TerrainRaycastServiceImpl(ITerrainRaycastProvider* provider)
        : provider(provider)
    {
    }

    TerrainRaycastServiceImpl::~TerrainRaycastServiceImpl()
    {
        unsubscribeAll();
    }

    void TerrainRaycastServiceImpl::unsubscribeAll()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (sculptModeToken.isValid())
            dispatcher.unsubscribe(sculptModeToken);

        if (paintModeToken.isValid())
            dispatcher.unsubscribe(paintModeToken);

        if (brushParamsToken.isValid())
            dispatcher.unsubscribe(brushParamsToken);

        if (paintBrushParamsToken.isValid())
            dispatcher.unsubscribe(paintBrushParamsToken);

        if (holeModeToken.isValid())
            dispatcher.unsubscribe(holeModeToken);

        if (holeBrushParamsToken.isValid())
            dispatcher.unsubscribe(holeBrushParamsToken);

        if (caveModeToken.isValid())
            dispatcher.unsubscribe(caveModeToken);

        if (caveBrushParamsToken.isValid())
            dispatcher.unsubscribe(caveBrushParamsToken);

        if (vegBrushModeToken.isValid())
            dispatcher.unsubscribe(vegBrushModeToken);

        if (vegBrushParamsToken.isValid())
            dispatcher.unsubscribe(vegBrushParamsToken);

        if (meshBrushModeToken.isValid())
            dispatcher.unsubscribe(meshBrushModeToken);

        if (meshBrushParamsToken.isValid())
            dispatcher.unsubscribe(meshBrushParamsToken);
    }

    void TerrainRaycastServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::terrainRaycast::SetCursorPositionCommand>(
            [this](const events::terrainRaycast::SetCursorPositionCommand& cmd)
            {
                if ((!sculptModeActive && !paintModeActive && !holeModeActive && !caveModeActive && !vegBrushModeActive && !meshBrushModeActive) || !provider)
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

        caveModeToken = dispatcher.subscribe<events::cave::CaveModeChangedNotification>(
            [this](const events::cave::CaveModeChangedNotification& n)
            {
                if (n.isActive)
                {
                    caveModeActive = true;
                    if (provider)
                    {
                        auto brushParams = events::EventDispatcher::instance().query(
                            events::caveBrush::GetCaveBrushParamsQuery{});
                        provider->setBrushOverlayParams(
                            brushParams.radius,
                            static_cast<float>(brushParams.falloff),
                            static_cast<float>(brushParams.shape));
                    }
                }
                else
                {
                    caveModeActive = false;
                    if (provider)
                    {
                        provider->clearRaycastCursor();
                        provider->setBrushOverlayParams(0.0f, 0.0f, 0.0f);
                    }
                }
            });

        caveBrushParamsToken = dispatcher.subscribe<events::caveBrush::CaveBrushParamsChangedNotification>(
            [this](const events::caveBrush::CaveBrushParamsChangedNotification& n)
            {
                if (caveModeActive && provider)
                {
                    provider->setBrushOverlayParams(
                        n.params.radius,
                        static_cast<float>(n.params.falloff),
                        static_cast<float>(n.params.shape));
                }
            });

        vegBrushModeToken = dispatcher.subscribe<events::vegetationBrush::VegetationBrushModeChangedNotification>(
            [this](const events::vegetationBrush::VegetationBrushModeChangedNotification& n)
            {
                if (n.isActive)
                {
                    vegBrushModeActive = true;
                    if (provider)
                    {
                        auto brushParams = events::EventDispatcher::instance().query(
                            events::vegetationBrush::GetVegetationBrushParamsQuery{});
                        provider->setBrushOverlayParams(
                            brushParams.radius,
                            static_cast<float>(brushParams.falloff),
                            0.0f);
                    }
                }
                else
                {
                    vegBrushModeActive = false;
                    if (provider)
                    {
                        provider->clearRaycastCursor();
                        provider->setBrushOverlayParams(0.0f, 0.0f, 0.0f);
                    }
                }
            });

        // Vegetation brush params change subscription removed - params now handled by VegetationBrushServiceImpl

        meshBrushModeToken = dispatcher.subscribe<events::meshBrush::MeshBrushModeChangedNotification>(
            [this](const events::meshBrush::MeshBrushModeChangedNotification& n)
            {
                if (n.isActive)
                {
                    meshBrushModeActive = true;
                    if (provider)
                    {
                        auto brushParams = events::EventDispatcher::instance().query(
                            events::meshBrush::GetMeshBrushParamsQuery{});
                        provider->setBrushOverlayParams(
                            brushParams.radius,
                            static_cast<float>(brushParams.falloff),
                            0.0f);
                    }
                }
                else
                {
                    meshBrushModeActive = false;
                    if (provider)
                    {
                        provider->clearRaycastCursor();
                        provider->setBrushOverlayParams(0.0f, 0.0f, 0.0f);
                    }
                }
            });

        meshBrushParamsToken = dispatcher.subscribe<events::meshBrush::MeshBrushParamsChangedNotification>(
            [this](const events::meshBrush::MeshBrushParamsChangedNotification& n)
            {
                if (meshBrushModeActive && provider)
                {
                    provider->setBrushOverlayParams(
                        n.params.radius,
                        static_cast<float>(n.params.falloff),
                        0.0f);
                }
            });
    }
}

#include "HoleBrushServiceImpl.hpp"
#include "../events/EventDispatcher.hpp"
#include "../events/HoleBrushEvents.hpp"
#include "../events/HoleModeEvents.hpp"

namespace services
{
    HoleBrushServiceImpl::~HoleBrushServiceImpl()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (holeModeToken.isValid())
        {
            dispatcher.unsubscribe(holeModeToken);
        }
    }

    void HoleBrushServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::holeBrush::SetHoleBrushParamsCommand>(
            [this](const events::holeBrush::SetHoleBrushParamsCommand& cmd)
            {
                setParams(cmd.params);
            });

        dispatcher.registerCommandHandler<events::holeBrush::SetHoleBrushRadiusCommand>(
            [this](const events::holeBrush::SetHoleBrushRadiusCommand& cmd)
            {
                setRadius(cmd.radius);
            });

        dispatcher.registerCommandHandler<events::holeBrush::SetHoleBrushFalloffCommand>(
            [this](const events::holeBrush::SetHoleBrushFalloffCommand& cmd)
            {
                if (!holeModeActive)
                {
                    return;
                }

                currentParams.falloff = cmd.falloff;
                publishParamsChanged();
            });

        dispatcher.registerCommandHandler<events::holeBrush::SetHoleBrushShapeCommand>(
            [this](const events::holeBrush::SetHoleBrushShapeCommand& cmd)
            {
                if (!holeModeActive)
                {
                    return;
                }

                currentParams.shape = cmd.shape;
                publishParamsChanged();
            });

        dispatcher.registerQueryHandler<events::holeBrush::GetHoleBrushParamsQuery>(
            [this](const events::holeBrush::GetHoleBrushParamsQuery&)
            {
                return getParams();
            });

        holeModeToken = dispatcher.subscribe<events::hole::HoleModeChangedNotification>(
            [this](const events::hole::HoleModeChangedNotification& n)
            {
                if (n.isActive)
                {
                    holeModeActive = true;
                }
                else
                {
                    holeModeActive = false;
                    currentParams = terrain::HoleBrushParams{};
                    publishParamsChanged();
                }
            });
    }

    void HoleBrushServiceImpl::setParams(const terrain::HoleBrushParams& params)
    {
        if (!holeModeActive)
        {
            return;
        }

        currentParams = params;
        currentParams.validate();
        publishParamsChanged();
    }

    void HoleBrushServiceImpl::setRadius(float radius)
    {
        if (!holeModeActive)
        {
            return;
        }

        currentParams.radius = radius;
        currentParams.validate();
        publishParamsChanged();
    }

    terrain::HoleBrushParams HoleBrushServiceImpl::getParams() const
    {
        return currentParams;
    }

    void HoleBrushServiceImpl::publishParamsChanged()
    {
        events::holeBrush::HoleBrushParamsChangedNotification notification;
        notification.params = currentParams;
        events::EventDispatcher::instance().publish(notification);
    }
}

#include "BrushServiceImpl.hpp"
#include "../events/EventDispatcher.hpp"
#include "../events/BrushEvents.hpp"
#include "../events/SculptModeEvents.hpp"

namespace services
{
    BrushServiceImpl::~BrushServiceImpl()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (sculptModeToken.isValid())
        {
            dispatcher.unsubscribe(sculptModeToken);
        }
    }

    void BrushServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::brush::SetBrushParamsCommand>(
            [this](const events::brush::SetBrushParamsCommand& cmd)
            {
                setParams(cmd.params);
            });

        dispatcher.registerCommandHandler<events::brush::SetBrushRadiusCommand>(
            [this](const events::brush::SetBrushRadiusCommand& cmd)
            {
                setRadius(cmd.radius);
            });

        dispatcher.registerCommandHandler<events::brush::SetBrushStrengthCommand>(
            [this](const events::brush::SetBrushStrengthCommand& cmd)
            {
                setStrength(cmd.strength);
            });

        dispatcher.registerCommandHandler<events::brush::SetBrushFalloffCommand>(
            [this](const events::brush::SetBrushFalloffCommand& cmd)
            {
                if (!sculptModeActive)
                {
                    return;
                }

                currentParams.falloff = cmd.falloff;
                publishParamsChanged();
            });

        dispatcher.registerCommandHandler<events::brush::SetBrushShapeCommand>(
            [this](const events::brush::SetBrushShapeCommand& cmd)
            {
                if (!sculptModeActive)
                {
                    return;
                }

                currentParams.shape = cmd.shape;
                publishParamsChanged();
            });

        dispatcher.registerQueryHandler<events::brush::GetBrushParamsQuery>(
            [this](const events::brush::GetBrushParamsQuery&)
            {
                return getParams();
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
                    currentParams = terrain::BrushParams{};
                    publishParamsChanged();
                }
            });
    }

    void BrushServiceImpl::setParams(const terrain::BrushParams& params)
    {
        if (!sculptModeActive)
        {
            return;
        }

        currentParams = params;
        currentParams.validate();
        publishParamsChanged();
    }

    void BrushServiceImpl::setRadius(float radius)
    {
        if (!sculptModeActive)
        {
            return;
        }

        currentParams.radius = radius;
        currentParams.validate();
        publishParamsChanged();
    }

    void BrushServiceImpl::setStrength(float strength)
    {
        if (!sculptModeActive)
        {
            return;
        }

        currentParams.strength = strength;
        currentParams.validate();
        publishParamsChanged();
    }

    terrain::BrushParams BrushServiceImpl::getParams() const
    {
        return currentParams;
    }

    void BrushServiceImpl::publishParamsChanged()
    {
        events::brush::BrushParamsChangedNotification notification;
        notification.params = currentParams;
        events::EventDispatcher::instance().publish(notification);
    }
}

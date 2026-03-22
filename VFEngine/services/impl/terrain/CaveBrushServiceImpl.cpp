#include "CaveBrushServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/terrain/CaveBrushEvents.hpp"
#include "../../events/terrain/CaveModeEvents.hpp"

namespace services
{
    CaveBrushServiceImpl::~CaveBrushServiceImpl()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (caveModeToken.isValid())
        {
            dispatcher.unsubscribe(caveModeToken);
        }
    }

    void CaveBrushServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::caveBrush::SetCaveBrushParamsCommand>(
            [this](const events::caveBrush::SetCaveBrushParamsCommand& cmd)
            {
                setParams(cmd.params);
            });

        dispatcher.registerCommandHandler<events::caveBrush::SetCaveBrushRadiusCommand>(
            [this](const events::caveBrush::SetCaveBrushRadiusCommand& cmd)
            {
                setRadius(cmd.radius);
            });

        dispatcher.registerCommandHandler<events::caveBrush::SetCaveBrushStrengthCommand>(
            [this](const events::caveBrush::SetCaveBrushStrengthCommand& cmd)
            {
                setStrength(cmd.strength);
            });

        dispatcher.registerCommandHandler<events::caveBrush::SetCaveBrushFalloffCommand>(
            [this](const events::caveBrush::SetCaveBrushFalloffCommand& cmd)
            {
                if (!caveModeActive)
                    return;

                currentParams.falloff = cmd.falloff;
                publishParamsChanged();
            });

        dispatcher.registerCommandHandler<events::caveBrush::SetCaveBrushShapeCommand>(
            [this](const events::caveBrush::SetCaveBrushShapeCommand& cmd)
            {
                if (!caveModeActive)
                    return;

                currentParams.shape = cmd.shape;
                publishParamsChanged();
            });

        dispatcher.registerCommandHandler<events::caveBrush::SetCaveBrushTypeCommand>(
            [this](const events::caveBrush::SetCaveBrushTypeCommand& cmd)
            {
                setBrushType(cmd.type);
            });

        dispatcher.registerQueryHandler<events::caveBrush::GetCaveBrushParamsQuery>(
            [this](const events::caveBrush::GetCaveBrushParamsQuery&)
            {
                return getParams();
            });

        dispatcher.registerQueryHandler<events::caveBrush::GetCaveBrushTypeQuery>(
            [this](const events::caveBrush::GetCaveBrushTypeQuery&)
            {
                return getBrushType();
            });

        caveModeToken = dispatcher.subscribe<events::cave::CaveModeChangedNotification>(
            [this](const events::cave::CaveModeChangedNotification& n)
            {
                caveModeActive = n.isActive;
            });
    }

    void CaveBrushServiceImpl::setParams(const terrain::CaveBrushParams& params)
    {
        if (!caveModeActive)
            return;

        currentParams = params;
        currentParams.validate();
        publishParamsChanged();
    }

    void CaveBrushServiceImpl::setRadius(float radius)
    {
        if (!caveModeActive)
            return;

        currentParams.radius = radius;
        currentParams.validate();
        publishParamsChanged();
    }

    void CaveBrushServiceImpl::setStrength(float strength)
    {
        if (!caveModeActive)
            return;

        currentParams.strength = strength;
        currentParams.validate();
        publishParamsChanged();
    }

    void CaveBrushServiceImpl::setBrushType(terrain::CaveBrushType type)
    {
        if (!caveModeActive)
            return;

        currentBrushType = type;
        publishTypeChanged();
    }

    terrain::CaveBrushParams CaveBrushServiceImpl::getParams() const
    {
        return currentParams;
    }

    terrain::CaveBrushType CaveBrushServiceImpl::getBrushType() const
    {
        return currentBrushType;
    }

    void CaveBrushServiceImpl::publishParamsChanged()
    {
        events::caveBrush::CaveBrushParamsChangedNotification notification;
        notification.params = currentParams;
        events::EventDispatcher::instance().publish(notification);
    }

    void CaveBrushServiceImpl::publishTypeChanged()
    {
        events::caveBrush::CaveBrushTypeChangedNotification notification;
        notification.type = currentBrushType;
        events::EventDispatcher::instance().publish(notification);
    }
}

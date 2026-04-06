#include "BrushServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/terrain/BrushEvents.hpp"
#include "../../events/editor/SculptModeEvents.hpp"

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

        dispatcher.registerCommandHandler<events::brush::SetBrushTypeCommand>(
            [this](const events::brush::SetBrushTypeCommand& cmd)
            {
                setBrushType(cmd.type);
            });

        dispatcher.registerQueryHandler<events::brush::GetBrushParamsQuery>(
            [this](const events::brush::GetBrushParamsQuery&)
            {
                return getParams();
            });

        dispatcher.registerQueryHandler<events::brush::GetBrushTypeQuery>(
            [this](const events::brush::GetBrushTypeQuery&)
            {
                return getBrushType();
            });

        dispatcher.registerCommandHandler<events::brush::SetStampImageCommand>(
            [this](const events::brush::SetStampImageCommand& cmd)
            {
                stampImagePath = cmd.filePath;
                stampData = terrain::HeightmapLoader::load(cmd.filePath);

                currentParams.stampImagePath = cmd.filePath;
                publishParamsChanged();

                events::brush::StampImageChangedNotification notification;
                notification.filePath = cmd.filePath;
                notification.loaded = (stampData != nullptr && stampData->isValid());
                events::EventDispatcher::instance().publish(notification);
            });

        dispatcher.registerCommandHandler<events::brush::SetStampRotationCommand>(
            [this](const events::brush::SetStampRotationCommand& cmd)
            {
                stampRotation = cmd.rotation;
                currentParams.stampRotation = cmd.rotation;
                publishParamsChanged();
            });

        dispatcher.registerCommandHandler<events::brush::SetStampScaleCommand>(
            [this](const events::brush::SetStampScaleCommand& cmd)
            {
                stampScale = cmd.scale;
                currentParams.stampScale = cmd.scale;
                publishParamsChanged();
            });

        dispatcher.registerQueryHandler<events::brush::GetStampDataQuery>(
            [this](const events::brush::GetStampDataQuery&)
            {
                return stampData;
            });

        sculptModeToken = dispatcher.subscribe<events::sculpt::SculptModeChangedNotification>(
            [this](const events::sculpt::SculptModeChangedNotification& n)
            {
                sculptModeActive = n.isActive;
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

    void BrushServiceImpl::setBrushType(terrain::BrushType type)
    {
        if (!sculptModeActive)
        {
            return;
        }

        currentBrushType = type;
        publishTypeChanged();
    }

    terrain::BrushType BrushServiceImpl::getBrushType() const
    {
        return currentBrushType;
    }

    void BrushServiceImpl::publishParamsChanged()
    {
        events::brush::BrushParamsChangedNotification notification;
        notification.params = currentParams;
        events::EventDispatcher::instance().publish(notification);
    }

    void BrushServiceImpl::publishTypeChanged()
    {
        events::brush::BrushTypeChangedNotification notification;
        notification.type = currentBrushType;
        events::EventDispatcher::instance().publish(notification);
    }
}

#include "PaintBrushServiceImpl.hpp"
#include "../events/EventDispatcher.hpp"
#include "../events/PaintBrushEvents.hpp"
#include "../events/PaintModeEvents.hpp"

namespace services
{
    PaintBrushServiceImpl::~PaintBrushServiceImpl()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (paintModeToken.isValid())
        {
            dispatcher.unsubscribe(paintModeToken);
        }
    }

    void PaintBrushServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::paintBrush::SetPaintBrushParamsCommand>(
            [this](const events::paintBrush::SetPaintBrushParamsCommand& cmd)
            {
                setParams(cmd.params);
            });

        dispatcher.registerCommandHandler<events::paintBrush::SetPaintBrushRadiusCommand>(
            [this](const events::paintBrush::SetPaintBrushRadiusCommand& cmd)
            {
                setRadius(cmd.radius);
            });

        dispatcher.registerCommandHandler<events::paintBrush::SetPaintBrushStrengthCommand>(
            [this](const events::paintBrush::SetPaintBrushStrengthCommand& cmd)
            {
                setStrength(cmd.strength);
            });

        dispatcher.registerCommandHandler<events::paintBrush::SetPaintBrushOpacityCommand>(
            [this](const events::paintBrush::SetPaintBrushOpacityCommand& cmd)
            {
                setOpacity(cmd.opacity);
            });

        dispatcher.registerCommandHandler<events::paintBrush::SetPaintActiveLayerCommand>(
            [this](const events::paintBrush::SetPaintActiveLayerCommand& cmd)
            {
                setActiveLayer(cmd.layer);
            });

        dispatcher.registerCommandHandler<events::paintBrush::SetPaintBrushFalloffCommand>(
            [this](const events::paintBrush::SetPaintBrushFalloffCommand& cmd)
            {
                if (!paintModeActive)
                {
                    return;
                }

                currentParams.falloff = cmd.falloff;
                publishParamsChanged();
            });

        dispatcher.registerCommandHandler<events::paintBrush::SetPaintBrushShapeCommand>(
            [this](const events::paintBrush::SetPaintBrushShapeCommand& cmd)
            {
                if (!paintModeActive)
                {
                    return;
                }

                currentParams.shape = cmd.shape;
                publishParamsChanged();
            });

        dispatcher.registerCommandHandler<events::paintBrush::SetPaintBrushTypeCommand>(
            [this](const events::paintBrush::SetPaintBrushTypeCommand& cmd)
            {
                setBrushType(cmd.type);
            });

        dispatcher.registerQueryHandler<events::paintBrush::GetPaintBrushParamsQuery>(
            [this](const events::paintBrush::GetPaintBrushParamsQuery&)
            {
                return getParams();
            });

        dispatcher.registerQueryHandler<events::paintBrush::GetPaintBrushTypeQuery>(
            [this](const events::paintBrush::GetPaintBrushTypeQuery&)
            {
                return getBrushType();
            });

        paintModeToken = dispatcher.subscribe<events::paint::PaintModeChangedNotification>(
            [this](const events::paint::PaintModeChangedNotification& n)
            {
                if (n.isActive)
                {
                    paintModeActive = true;
                }
                else
                {
                    paintModeActive = false;
                    currentParams = terrain::PaintBrushParams{};
                    currentBrushType = terrain::PaintBrushType::PaintLayer;
                    publishParamsChanged();
                    publishTypeChanged();
                }
            });
    }

    void PaintBrushServiceImpl::setParams(const terrain::PaintBrushParams& params)
    {
        if (!paintModeActive)
        {
            return;
        }

        currentParams = params;
        currentParams.validate();
        publishParamsChanged();
    }

    void PaintBrushServiceImpl::setRadius(float radius)
    {
        if (!paintModeActive)
        {
            return;
        }

        currentParams.radius = radius;
        currentParams.validate();
        publishParamsChanged();
    }

    void PaintBrushServiceImpl::setStrength(float strength)
    {
        if (!paintModeActive)
        {
            return;
        }

        currentParams.strength = strength;
        currentParams.validate();
        publishParamsChanged();
    }

    void PaintBrushServiceImpl::setOpacity(float opacity)
    {
        if (!paintModeActive)
        {
            return;
        }

        currentParams.opacity = opacity;
        currentParams.validate();
        publishParamsChanged();
    }

    void PaintBrushServiceImpl::setActiveLayer(uint32_t layer)
    {
        if (!paintModeActive)
        {
            return;
        }

        currentParams.activeLayer = layer;
        publishParamsChanged();
    }

    terrain::PaintBrushParams PaintBrushServiceImpl::getParams() const
    {
        return currentParams;
    }

    void PaintBrushServiceImpl::setBrushType(terrain::PaintBrushType type)
    {
        if (!paintModeActive)
        {
            return;
        }

        currentBrushType = type;
        publishTypeChanged();
    }

    terrain::PaintBrushType PaintBrushServiceImpl::getBrushType() const
    {
        return currentBrushType;
    }

    void PaintBrushServiceImpl::publishParamsChanged()
    {
        events::paintBrush::PaintBrushParamsChangedNotification notification;
        notification.params = currentParams;
        events::EventDispatcher::instance().publish(notification);
    }

    void PaintBrushServiceImpl::publishTypeChanged()
    {
        events::paintBrush::PaintBrushTypeChangedNotification notification;
        notification.type = currentBrushType;
        events::EventDispatcher::instance().publish(notification);
    }
}

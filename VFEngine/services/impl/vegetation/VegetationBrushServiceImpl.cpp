#include "VegetationBrushServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/vegetation/VegetationBrushEvents.hpp"

namespace services
{
    VegetationBrushServiceImpl::~VegetationBrushServiceImpl()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (vegetationModeToken.isValid())
            dispatcher.unsubscribe(vegetationModeToken);
    }

    void VegetationBrushServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Density brush param commands
        dispatcher.registerCommandHandler<events::vegetationBrush::SetDensityBrushParamsCommand>(
            [this](const events::vegetationBrush::SetDensityBrushParamsCommand& cmd)
            {
                setDensityParams(cmd.params);
            });

        // Density brush type command
        dispatcher.registerCommandHandler<events::vegetationBrush::SetDensityBrushTypeCommand>(
            [this](const events::vegetationBrush::SetDensityBrushTypeCommand& cmd)
            {
                setDensityBrushType(cmd.type);
            });

        // Apply brush commands are handled by TerrainService (which has grid access)

        // Queries
        dispatcher.registerQueryHandler<events::vegetationBrush::GetDensityBrushParamsQuery>(
            [this](const events::vegetationBrush::GetDensityBrushParamsQuery&)
            {
                return getDensityParams();
            });

        dispatcher.registerQueryHandler<events::vegetationBrush::GetDensityBrushTypeQuery>(
            [this](const events::vegetationBrush::GetDensityBrushTypeQuery&)
            {
                return getDensityBrushType();
            });

        // Subscribe to vegetation brush mode changes
        vegetationModeToken = dispatcher.subscribe<events::vegetationBrush::VegetationBrushModeChangedNotification>(
            [this](const events::vegetationBrush::VegetationBrushModeChangedNotification& n)
            {
                vegetationModeActive = n.isActive;
            });
    }

    void VegetationBrushServiceImpl::setDensityParams(const vegetation::DensityBrushParams& params)
    {
        if (!vegetationModeActive)
        {
            return;
        }

        currentDensityParams = params;
        currentDensityParams.validate();
        publishDensityParamsChanged();
    }

    void VegetationBrushServiceImpl::setDensityBrushType(vegetation::DensityBrushType type)
    {
        if (!vegetationModeActive)
        {
            return;
        }

        currentDensityBrushType = type;
        publishDensityTypeChanged();
    }

    vegetation::DensityBrushParams VegetationBrushServiceImpl::getDensityParams() const
    {
        return currentDensityParams;
    }

    vegetation::DensityBrushType VegetationBrushServiceImpl::getDensityBrushType() const
    {
        return currentDensityBrushType;
    }

    void VegetationBrushServiceImpl::publishDensityParamsChanged()
    {
        events::vegetationBrush::DensityBrushParamsChangedNotification notification;
        notification.params = currentDensityParams;
        events::EventDispatcher::instance().publish(notification);
    }

    void VegetationBrushServiceImpl::publishDensityTypeChanged()
    {
        events::vegetationBrush::DensityBrushTypeChangedNotification notification;
        notification.type = currentDensityBrushType;
        events::EventDispatcher::instance().publish(notification);
    }
}

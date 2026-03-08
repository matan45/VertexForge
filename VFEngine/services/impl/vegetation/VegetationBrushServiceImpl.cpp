#include "VegetationBrushServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/vegetation/VegetationBrushEvents.hpp"

namespace services
{
    VegetationBrushServiceImpl::~VegetationBrushServiceImpl()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (vegetationModeToken.isValid())
        {
            dispatcher.unsubscribe(vegetationModeToken);
        }
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

        // Placement brush param commands
        dispatcher.registerCommandHandler<events::vegetationBrush::SetPlacementBrushParamsCommand>(
            [this](const events::vegetationBrush::SetPlacementBrushParamsCommand& cmd)
            {
                setPlacementParams(cmd.params);
            });

        // Density brush type command
        dispatcher.registerCommandHandler<events::vegetationBrush::SetDensityBrushTypeCommand>(
            [this](const events::vegetationBrush::SetDensityBrushTypeCommand& cmd)
            {
                setDensityBrushType(cmd.type);
            });

        // Placement brush type command
        dispatcher.registerCommandHandler<events::vegetationBrush::SetPlacementBrushTypeCommand>(
            [this](const events::vegetationBrush::SetPlacementBrushTypeCommand& cmd)
            {
                setPlacementBrushType(cmd.type);
            });

        // Apply brush commands are handled by TerrainService (which has grid access)

        // Queries
        dispatcher.registerQueryHandler<events::vegetationBrush::GetDensityBrushParamsQuery>(
            [this](const events::vegetationBrush::GetDensityBrushParamsQuery&)
            {
                return getDensityParams();
            });

        dispatcher.registerQueryHandler<events::vegetationBrush::GetPlacementBrushParamsQuery>(
            [this](const events::vegetationBrush::GetPlacementBrushParamsQuery&)
            {
                return getPlacementParams();
            });

        dispatcher.registerQueryHandler<events::vegetationBrush::GetDensityBrushTypeQuery>(
            [this](const events::vegetationBrush::GetDensityBrushTypeQuery&)
            {
                return getDensityBrushType();
            });

        dispatcher.registerQueryHandler<events::vegetationBrush::GetPlacementBrushTypeQuery>(
            [this](const events::vegetationBrush::GetPlacementBrushTypeQuery&)
            {
                return getPlacementBrushType();
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

    void VegetationBrushServiceImpl::setPlacementParams(const vegetation::PlacementBrushParams& params)
    {
        if (!vegetationModeActive)
        {
            return;
        }

        currentPlacementParams = params;
        currentPlacementParams.validate();
        publishPlacementParamsChanged();
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

    void VegetationBrushServiceImpl::setPlacementBrushType(vegetation::PlacementBrushType type)
    {
        if (!vegetationModeActive)
        {
            return;
        }

        currentPlacementBrushType = type;
        publishPlacementTypeChanged();
    }

    vegetation::DensityBrushParams VegetationBrushServiceImpl::getDensityParams() const
    {
        return currentDensityParams;
    }

    vegetation::PlacementBrushParams VegetationBrushServiceImpl::getPlacementParams() const
    {
        return currentPlacementParams;
    }

    vegetation::DensityBrushType VegetationBrushServiceImpl::getDensityBrushType() const
    {
        return currentDensityBrushType;
    }

    vegetation::PlacementBrushType VegetationBrushServiceImpl::getPlacementBrushType() const
    {
        return currentPlacementBrushType;
    }

    void VegetationBrushServiceImpl::publishDensityParamsChanged()
    {
        events::vegetationBrush::DensityBrushParamsChangedNotification notification;
        notification.params = currentDensityParams;
        events::EventDispatcher::instance().publish(notification);
    }

    void VegetationBrushServiceImpl::publishPlacementParamsChanged()
    {
        events::vegetationBrush::PlacementBrushParamsChangedNotification notification;
        notification.params = currentPlacementParams;
        events::EventDispatcher::instance().publish(notification);
    }

    void VegetationBrushServiceImpl::publishDensityTypeChanged()
    {
        events::vegetationBrush::DensityBrushTypeChangedNotification notification;
        notification.type = currentDensityBrushType;
        events::EventDispatcher::instance().publish(notification);
    }

    void VegetationBrushServiceImpl::publishPlacementTypeChanged()
    {
        events::vegetationBrush::PlacementBrushTypeChangedNotification notification;
        notification.type = currentPlacementBrushType;
        events::EventDispatcher::instance().publish(notification);
    }
}

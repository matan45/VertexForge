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

        dispatcher.registerCommandHandler<events::brush::ClearStampImageCommand>(
            [this](const events::brush::ClearStampImageCommand&)
            {
                stampData = nullptr;
                stampImagePath.clear();
                stampRotation = 0.0f;
                stampScale = 1.0f;
                currentParams.stampImagePath.clear();
                currentParams.stampRotation = 0.0f;
                currentParams.stampScale = 1.0f;
                publishParamsChanged();

                events::brush::StampImageChangedNotification notification;
                notification.filePath = "";
                notification.loaded = false;
                events::EventDispatcher::instance().publish(notification);
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

        dispatcher.registerCommandHandler<events::brush::SetStampModeCommand>(
            [this](const events::brush::SetStampModeCommand& cmd)
            {
                currentParams.stampSubtract = cmd.subtract;
                publishParamsChanged();
            });

        dispatcher.registerCommandHandler<events::brush::SetTalusAngleCommand>(
            [this](const events::brush::SetTalusAngleCommand& cmd)
            {
                currentParams.talusAngle = cmd.angle;
                publishParamsChanged();
            });

        dispatcher.registerCommandHandler<events::brush::SetTerraceStepHeightCommand>(
            [this](const events::brush::SetTerraceStepHeightCommand& cmd)
            {
                currentParams.terraceStepHeight = cmd.stepHeight;
                publishParamsChanged();
            });

        dispatcher.registerCommandHandler<events::brush::SetTerraceSharpnessCommand>(
            [this](const events::brush::SetTerraceSharpnessCommand& cmd)
            {
                currentParams.terraceSharpness = cmd.sharpness;
                publishParamsChanged();
            });

        dispatcher.registerCommandHandler<events::brush::SetRampWidthCommand>(
            [this](const events::brush::SetRampWidthCommand& cmd)
            {
                currentParams.rampWidth = cmd.width;
                publishParamsChanged();
            });

        dispatcher.registerCommandHandler<events::brush::SetRampFalloffCommand>(
            [this](const events::brush::SetRampFalloffCommand& cmd)
            {
                currentParams.rampFalloff = cmd.falloff;
                publishParamsChanged();
            });

        // VK-1616. These call validate() explicitly, unlike the talus/terrace/ramp setters above:
        // BrushParams::validate() only runs from setParams/setRadius/setStrength today, and the
        // hydraulic solver is the one place where an out-of-range value is not merely an odd-looking
        // brush but a numerically unstable one.
        dispatcher.registerCommandHandler<events::brush::SetHydraulicRainRateCommand>(
            [this](const events::brush::SetHydraulicRainRateCommand& cmd)
            {
                currentParams.hydraulicRainRate = cmd.rainRate;
                currentParams.validate();
                publishParamsChanged();
            });

        dispatcher.registerCommandHandler<events::brush::SetHydraulicSedimentCapacityCommand>(
            [this](const events::brush::SetHydraulicSedimentCapacityCommand& cmd)
            {
                currentParams.hydraulicSedimentCapacity = cmd.capacity;
                currentParams.validate();
                publishParamsChanged();
            });

        dispatcher.registerCommandHandler<events::brush::SetHydraulicIterationsCommand>(
            [this](const events::brush::SetHydraulicIterationsCommand& cmd)
            {
                currentParams.hydraulicIterations = cmd.iterations;
                currentParams.validate();
                publishParamsChanged();
            });

        dispatcher.registerCommandHandler<events::brush::SetHydraulicEvaporationCommand>(
            [this](const events::brush::SetHydraulicEvaporationCommand& cmd)
            {
                currentParams.hydraulicEvaporation = cmd.evaporation;
                currentParams.validate();
                publishParamsChanged();
            });

        dispatcher.registerCommandHandler<events::brush::SetHydraulicHardnessCommand>(
            [this](const events::brush::SetHydraulicHardnessCommand& cmd)
            {
                currentParams.hydraulicHardness = cmd.hardness;
                currentParams.validate();
                publishParamsChanged();
            });

        dispatcher.registerCommandHandler<events::brush::SetHydraulicSmoothingCommand>(
            [this](const events::brush::SetHydraulicSmoothingCommand& cmd)
            {
                currentParams.hydraulicSmoothing = cmd.smoothing;
                currentParams.validate();
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

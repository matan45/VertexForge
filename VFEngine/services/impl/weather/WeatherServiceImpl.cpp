#include "WeatherServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/weather/WeatherEvents.hpp"
#include "../../events/render/CloudEvents.hpp"
#include "../../events/render/AtmosphereEvents.hpp"
#include "../../events/render/PostProcessEvents.hpp"
#include "../../events/vegetation/GrassEvents.hpp"
#include "weather/WeatherPresets.hpp"
#include <glm/glm.hpp>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace services
{
    void WeatherServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::weather::UpdateWeatherCommand>(
            [this](const events::weather::UpdateWeatherCommand& cmd) {
                onUpdate(cmd.deltaTime);
            });

        dispatcher.registerCommandHandler<events::weather::SetWeatherPresetCommand>(
            [this](const events::weather::SetWeatherPresetCommand& cmd) {
                const auto& preset = weather::getPreset(cmd.preset);
                stateMachine.transitionTo(preset, cmd.transitionDuration, cmd.easing);
            });

        dispatcher.registerCommandHandler<events::weather::SetWeatherStateCommand>(
            [this](const events::weather::SetWeatherStateCommand& cmd) {
                stateMachine.transitionTo(cmd.state, cmd.transitionDuration, cmd.easing);
            });

        dispatcher.registerCommandHandler<events::weather::SetWeatherImmediateCommand>(
            [this](const events::weather::SetWeatherImmediateCommand& cmd) {
                stateMachine.setImmediate(cmd.state);
            });

        dispatcher.registerCommandHandler<events::weather::QueueWeatherCommand>(
            [this](const events::weather::QueueWeatherCommand& cmd) {
                stateMachine.queueTransition(cmd.state, cmd.transitionDuration, cmd.easing);
            });

        dispatcher.registerCommandHandler<events::weather::SetWeatherScheduleCommand>(
            [this](const events::weather::SetWeatherScheduleCommand& cmd) {
                schedule.loadFromJson(cmd.scheduleJson);
            });

        dispatcher.registerCommandHandler<events::weather::SetWeatherScheduleEnabledCommand>(
            [this](const events::weather::SetWeatherScheduleEnabledCommand& cmd) {
                scheduleEnabled = cmd.enabled;
            });

        dispatcher.registerCommandHandler<events::weather::SetWeatherBiomeCommand>(
            [this](const events::weather::SetWeatherBiomeCommand& cmd) {
                schedule.setActiveBiome(cmd.biomeId);
            });

        dispatcher.registerCommandHandler<events::weather::SetWeatherEnabledCommand>(
            [this](const events::weather::SetWeatherEnabledCommand& cmd) {
                weatherEnabled = cmd.enabled;
            });

        dispatcher.registerQueryHandler<events::weather::GetWeatherStateQuery>(
            [this](const events::weather::GetWeatherStateQuery&) {
                return stateMachine.getCurrentState();
            });

        dispatcher.registerQueryHandler<events::weather::GetWeatherTransitionProgressQuery>(
            [this](const events::weather::GetWeatherTransitionProgressQuery&) {
                return stateMachine.getTransitionProgress();
            });

        dispatcher.registerQueryHandler<events::weather::IsWeatherEnabledQuery>(
            [this](const events::weather::IsWeatherEnabledQuery&) {
                return weatherEnabled;
            });

        dispatcher.registerQueryHandler<events::weather::IsWeatherScheduleEnabledQuery>(
            [this](const events::weather::IsWeatherScheduleEnabledQuery&) {
                return scheduleEnabled;
            });
    }

    void WeatherServiceImpl::onUpdate(float deltaTime)
    {
        if (!weatherEnabled)
            return;

        auto& dispatcher = ::events::EventDispatcher::instance();

        // If schedule is enabled, evaluate it to potentially trigger a new transition
        if (scheduleEnabled)
        {
            try
            {
                auto atmosSettings = dispatcher.query(events::atmosphere::GetAtmosphereSettingsQuery{});
                float timeOfDay = atmosSettings.timeOfDay;

                auto transition = schedule.evaluate(timeOfDay, deltaTime);
                if (transition.has_value())
                {
                    stateMachine.queueTransition(
                        transition->targetState,
                        transition->duration,
                        transition->easing
                    );
                }
            }
            catch (...) {}
        }

        // Track transition state for notification
        bool wasTransitioningBefore = stateMachine.isTransitioning();
        weather::WeatherState previousState = stateMachine.getCurrentState();

        stateMachine.update(deltaTime);

        applyWeatherToPipelines(stateMachine.getCurrentState());

        // Publish notification when transition completes
        if (wasTransitioningBefore && !stateMachine.isTransitioning())
        {
            events::weather::WeatherStateChangedNotification notification;
            notification.previousState = previousState;
            notification.newState = stateMachine.getCurrentState();
            dispatcher.publish(notification);
        }
    }

    void WeatherServiceImpl::applyWeatherToPipelines(const weather::WeatherState& ws)
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        // --- Cloud Pipeline ---
        try
        {
            auto cloudSettings = dispatcher.query(events::cloud::GetCloudSettingsQuery{});
            cloudSettings.globalCoverage = ws.cloudCoverage;
            cloudSettings.globalDensity = ws.cloudDensity;
            cloudSettings.cloudType = ws.cloudType;
            cloudSettings.windSpeed = ws.windSpeed;
            cloudSettings.windDirectionDeg = ws.windDirectionDeg;
            cloudSettings.cloudColorTint = ws.atmosphereTint;
            cloudSettings.ambientIntensity = ws.ambientLightMult;

            events::cloud::ApplyCloudSettingsCommand cloudCmd;
            cloudCmd.settings = cloudSettings;
            dispatcher.execute(cloudCmd);
        }
        catch (...) {}

        // --- Atmosphere Pipeline ---
        try
        {
            auto atmosSettings = dispatcher.query(events::atmosphere::GetAtmosphereSettingsQuery{});
            atmosSettings.sunIrradiance *= ws.atmosphereTint;
            atmosSettings.aerialIntensity *= ws.ambientLightMult;

            events::atmosphere::ApplyAtmosphereSettingsCommand atmosCmd;
            atmosCmd.settings = atmosSettings;
            dispatcher.execute(atmosCmd);
        }
        catch (...) {}

        // --- Volumetric Fog ---
        try
        {
            auto ppSettings = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
            ppSettings.volumetricFog.uniformDensity = ws.fogDensity;
            ppSettings.volumetricFog.heightFogDensity = ws.heightFogDensity;
            ppSettings.volumetricFog.fogColor[0] = ws.atmosphereTint.x;
            ppSettings.volumetricFog.fogColor[1] = ws.atmosphereTint.y;
            ppSettings.volumetricFog.fogColor[2] = ws.atmosphereTint.z;

            events::postprocess::ApplyPostProcessSettingsCommand ppCmd;
            ppCmd.settings = ppSettings;
            dispatcher.execute(ppCmd);
        }
        catch (...) {}

        // --- Wind + Grass Billboards ---
        try
        {
            auto grassConfig = dispatcher.query(events::vegetation::GetGlobalGrassConfigQuery{});

            // Convert wind direction from degrees to normalized vec3 (XZ plane)
            float radians = ws.windDirectionDeg * static_cast<float>(M_PI) / 180.0f;
            grassConfig.windDirection = glm::vec3(std::cos(radians), 0.0f, std::sin(radians));
            grassConfig.windSpeed = ws.windSpeed;
            grassConfig.windStrength = std::clamp(ws.windSpeed / 40.0f, 0.0f, 1.0f);
            grassConfig.gustStrength = ws.gustStrength;
            grassConfig.gustFrequency = ws.gustFrequency;

            events::vegetation::SetGlobalGrassConfigCommand grassCmd;
            grassCmd.config = grassConfig;
            dispatcher.execute(grassCmd);
        }
        catch (...) {}
    }
}

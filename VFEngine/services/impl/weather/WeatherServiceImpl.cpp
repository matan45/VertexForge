#include "WeatherServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/weather/WeatherEvents.hpp"
#include "../../events/render/CloudEvents.hpp"
#include "../../events/render/AtmosphereEvents.hpp"
#include "../../events/render/PostProcessEvents.hpp"
#include "../../events/vegetation/GrassEvents.hpp"
#include "../../events/audio/AudioEvents.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../providers/vfx/IVFXRuntimeProvider.hpp"
#include "weather/WeatherPresets.hpp"
#include <glm/glm.hpp>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace services
{
    WeatherServiceImpl::WeatherServiceImpl(IVFXRuntimeProvider* vfxProv)
        : vfxProvider(vfxProv)
    {
        rainController = std::make_unique<RainController>();
        snowController = std::make_unique<SnowController>();
        if (vfxProvider)
        {
            rainController->init(vfxProvider);
            snowController->init(vfxProvider);
        }
        audioController.init();
    }

    WeatherServiceImpl::~WeatherServiceImpl()
    {
        if (rainController)
            rainController->cleanup();
        if (snowController)
            snowController->cleanup();
        audioController.cleanup();
    }

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

        dispatcher.registerCommandHandler<events::weather::SetWeatherAudioConfigCommand>(
            [this](const events::weather::SetWeatherAudioConfigCommand& cmd) {
                audioController.setConfig(cmd.config);
            });

        dispatcher.registerQueryHandler<events::weather::GetWeatherAudioConfigQuery>(
            [this](const events::weather::GetWeatherAudioConfigQuery&) {
                return audioController.getConfig();
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

        // Query camera position (used by zones and lightning)
        try
        {
            auto camOpt = dispatcher.query(events::scene::GetPrimaryCameraQuery{});
            if (camOpt.has_value())
            {
                events::scene::GetWorldTransformQuery tq;
                tq.entity = camOpt.value();
                auto xformOpt = dispatcher.query(tq);
                if (xformOpt.has_value())
                    cachedCameraPos = xformOpt->position;
            }
        }
        catch (...) {}

        // Evaluate weather zones to get effective weather at camera position
        const auto& globalWeather = stateMachine.getCurrentState();
        weather::WeatherState effectiveWeather = zoneEvaluator.evaluate(cachedCameraPos, globalWeather);

        // Publish zone enter/exit notifications
        for (const auto& transition : zoneEvaluator.getTransitions())
        {
            if (transition.entered)
            {
                events::weather::WeatherZoneEnteredNotification n;
                n.entityId = transition.entityId;
                dispatcher.publish(n);
            }
            else
            {
                events::weather::WeatherZoneExitedNotification n;
                n.entityId = transition.entityId;
                dispatcher.publish(n);
            }
        }

        applyWeatherToPipelines(effectiveWeather);
        updatePrecipitation(deltaTime, effectiveWeather);
        audioController.update(deltaTime, effectiveWeather);

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

            // Lightning flash boost on clouds
            auto lightning = lightningGenerator.getOutput();
            if (lightning.flashIntensity > 0.0f)
            {
                cloudSettings.ambientIntensity += lightning.flashIntensity * 1.6f;
                cloudSettings.cloudColorTint = glm::mix(
                    cloudSettings.cloudColorTint, glm::vec3(1.0f), lightning.flashIntensity * 0.5f);
            }

            events::cloud::ApplyCloudSettingsCommand cloudCmd;
            cloudCmd.settings = cloudSettings;
            dispatcher.execute(cloudCmd);
        }
        catch (...) {}

        // --- Atmosphere Pipeline ---
        try
        {
            auto atmosSettings = dispatcher.query(events::atmosphere::GetAtmosphereSettingsQuery{});
            // Capture base values once so we multiply from the original, not compounding each frame
            if (!basesAtmosCaptured)
            {
                baseSunIrradiance = atmosSettings.sunIrradiance;
                baseAerialIntensity = atmosSettings.aerialIntensity;
                basesAtmosCaptured = true;
            }
            atmosSettings.sunIrradiance = baseSunIrradiance * ws.atmosphereTint;
            atmosSettings.aerialIntensity = baseAerialIntensity * ws.ambientLightMult;

            // Lightning white flash on atmosphere
            auto lightningAtmos = lightningGenerator.getOutput();
            if (lightningAtmos.flashIntensity > 0.0f)
            {
                atmosSettings.sunIrradiance *= (1.0f + lightningAtmos.flashIntensity * 2.0f);
            }

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

    void WeatherServiceImpl::updatePrecipitation(float deltaTime, const weather::WeatherState& state)
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        bool isRain = state.precipType == weather::PrecipitationType::Rain;
        bool isSnow = state.precipType == weather::PrecipitationType::Snow;

        // Create zero-intensity state for inactive controller
        weather::WeatherState zeroState = state;
        zeroState.precipIntensity = 0.0f;

        // Route precipitation to the correct controller
        if (rainController)
            rainController->update(deltaTime, isRain ? state : zeroState);
        if (snowController)
            snowController->update(deltaTime, isSnow ? state : zeroState);

        // Ensure VFX runtime updates in editor mode (normally only in play mode)
        if (vfxProvider && vfxProvider->isInitialized() &&
            ((rainController && rainController->isActive()) || (snowController && snowController->isActive())))
        {
            vfxProvider->update(deltaTime);
        }

        // Track snow accumulation over time
        if (isSnow && state.precipIntensity > 0.01f)
            snowAccumulation = std::min(1.0f, snowAccumulation + deltaTime * state.precipIntensity * 0.05f);
        else
            snowAccumulation = std::max(0.0f, snowAccumulation - deltaTime * 0.02f);

        // Push snow accumulation to render side
        try
        {
            events::weather::SetSnowAccumulationCommand snowCmd;
            snowCmd.accumulation = snowAccumulation;
            dispatcher.execute(snowCmd);
        }
        catch (...) {}

        // Drive screen-space rain droplets only for rain
        try
        {
            auto ppSettings = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
            bool shouldEnableDroplets = isRain && state.precipIntensity > 0.01f;
            ppSettings.rainDroplets.enabled = shouldEnableDroplets;
            ppSettings.rainDroplets.intensity = isRain ? state.precipIntensity : 0.0f;

            events::postprocess::ApplyPostProcessSettingsCommand ppCmd;
            ppCmd.settings = ppSettings;
            dispatcher.execute(ppCmd);
        }
        catch (...) {}

        // --- Lightning & Thunder ---
        lightningGenerator.update(deltaTime, state, cachedCameraPos);
        auto lightning = lightningGenerator.getOutput();

        // Dispatch thunder audio at strike position
        if (lightning.shouldPlayThunder)
        {
            const auto& thunderPaths = audioController.getConfig().thunderPaths;
            int idx = lightning.thunderSoundIndex % 3;

            if (!thunderPaths[idx].empty())
            {
            try
            {
                events::audio::PlaySound3DCommand cmd;
                cmd.path = thunderPaths[idx];
                cmd.position = lightning.thunderPosition;
                cmd.params.is3D = true;
                cmd.params.volume = 0.9f;
                cmd.params.minDistance = 50.0f;
                cmd.params.maxDistance = 6000.0f;
                cmd.params.rolloffFactor = 0.8f;
                cmd.params.enableDistanceFilter = true;
                cmd.params.filterStartDistance = 500.0f;
                cmd.params.filterMaxDistance = 5000.0f;
                cmd.params.filterIntensity = 0.7f;
                dispatcher.execute(cmd);
            }
            catch (...) {}
            } // if thunderPath not empty

            // Publish notification for external listeners
            try
            {
                events::weather::LightningStrikeNotification notification;
                notification.position = lightning.thunderPosition;
                notification.intensity = 1.0f;
                dispatcher.publish(notification);
            }
            catch (...) {}
        }
    }
}

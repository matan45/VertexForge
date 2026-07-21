#include "WeatherServiceImpl.hpp"
#include "RainController.hpp"
#include "SnowController.hpp"
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
        if (rainController) rainController->cleanup();
        if (snowController) snowController->cleanup();
        audioController.cleanup();
    }

    void WeatherServiceImpl::registerEventHandlers()
    {
        registerWeatherCommands();
        registerWeatherQueries();
    }

    void WeatherServiceImpl::registerWeatherCommands()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        dispatcher.registerCommandHandler<events::weather::UpdateWeatherCommand>(
            [this](const events::weather::UpdateWeatherCommand& cmd) { onUpdate(cmd.deltaTime); });

        dispatcher.registerCommandHandler<events::weather::SetWeatherPresetCommand>(
            [this](const events::weather::SetWeatherPresetCommand& cmd) {
                stateMachine.transitionTo(weather::getPreset(cmd.preset), cmd.transitionDuration, cmd.easing);
            });

        dispatcher.registerCommandHandler<events::weather::SetWeatherStateCommand>(
            [this](const events::weather::SetWeatherStateCommand& cmd) {
                stateMachine.transitionTo(cmd.state, cmd.transitionDuration, cmd.easing);
            });

        dispatcher.registerCommandHandler<events::weather::SetWeatherImmediateCommand>(
            [this](const events::weather::SetWeatherImmediateCommand& cmd) { stateMachine.setImmediate(cmd.state); });

        dispatcher.registerCommandHandler<events::weather::QueueWeatherCommand>(
            [this](const events::weather::QueueWeatherCommand& cmd) {
                stateMachine.queueTransition(cmd.state, cmd.transitionDuration, cmd.easing);
            });

        dispatcher.registerCommandHandler<events::weather::SetWeatherScheduleCommand>(
            [this](const events::weather::SetWeatherScheduleCommand& cmd) { schedule.loadFromJson(cmd.scheduleJson); });

        dispatcher.registerCommandHandler<events::weather::SetWeatherScheduleEnabledCommand>(
            [this](const events::weather::SetWeatherScheduleEnabledCommand& cmd) { scheduleEnabled = cmd.enabled; });

        dispatcher.registerCommandHandler<events::weather::SetWeatherBiomeCommand>(
            [this](const events::weather::SetWeatherBiomeCommand& cmd) { schedule.setActiveBiome(cmd.biomeId); });

        dispatcher.registerCommandHandler<events::weather::SetWeatherEnabledCommand>(
            [this](const events::weather::SetWeatherEnabledCommand& cmd)
            {
                bool wasEnabled = weatherEnabled;
                weatherEnabled = cmd.enabled;
                if (wasEnabled && !cmd.enabled)
                    cleanupEffects();
            });

        dispatcher.registerCommandHandler<events::weather::SetWeatherAudioConfigCommand>(
            [this](const events::weather::SetWeatherAudioConfigCommand& cmd) { audioController.setConfig(cmd.config); });
    }

    void WeatherServiceImpl::registerWeatherQueries()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        dispatcher.registerQueryHandler<events::weather::GetWeatherAudioConfigQuery>(
            [this](const events::weather::GetWeatherAudioConfigQuery&) { return audioController.getConfig(); });

        dispatcher.registerQueryHandler<events::weather::GetWeatherStateQuery>(
            [this](const events::weather::GetWeatherStateQuery&) { return stateMachine.getCurrentState(); });

        dispatcher.registerQueryHandler<events::weather::GetWeatherTransitionProgressQuery>(
            [this](const events::weather::GetWeatherTransitionProgressQuery&) { return stateMachine.getTransitionProgress(); });

        dispatcher.registerQueryHandler<events::weather::IsWeatherEnabledQuery>(
            [this](const events::weather::IsWeatherEnabledQuery&) { return weatherEnabled; });

        dispatcher.registerQueryHandler<events::weather::IsWeatherScheduleEnabledQuery>(
            [this](const events::weather::IsWeatherScheduleEnabledQuery&) { return scheduleEnabled; });

        dispatcher.registerQueryHandler<events::weather::GetSnowAccumulationQuery>(
            [this](const events::weather::GetSnowAccumulationQuery&) { return snowAccumulation; });

        dispatcher.registerQueryHandler<events::weather::GetWetnessQuery>(
            [this](const events::weather::GetWetnessQuery&) { return wetness; });
    }

    void WeatherServiceImpl::onUpdate(float deltaTime)
    {
        if (!weatherEnabled) return;

        auto& dispatcher = ::events::EventDispatcher::instance();

        if (scheduleEnabled)
        {
            try
            {
                auto atmosSettings = dispatcher.query(events::atmosphere::GetAtmosphereSettingsQuery{});
                auto transition = schedule.evaluate(atmosSettings.timeOfDay, deltaTime);
                if (transition.has_value())
                    stateMachine.queueTransition(transition->targetState, transition->duration, transition->easing);
            }
            catch (...) {}
        }

        bool wasTransitioning = stateMachine.isTransitioning();
        weather::WeatherState previousState = stateMachine.getCurrentState();
        stateMachine.update(deltaTime);

        cachedCameraPos = queryCameraPosition();
        weather::WeatherState effectiveWeather = evaluateZones(stateMachine.getCurrentState());
        publishZoneTransitions();

        applyCloudWeather(effectiveWeather);
        applyAtmosphereWeather(effectiveWeather);
        applyFogWeather(effectiveWeather);
        applyWindWeather(effectiveWeather);

        updateAccumulation(deltaTime, effectiveWeather);
        updateScreenEffects(effectiveWeather);
        updateLightning(deltaTime, effectiveWeather);
        audioController.update(deltaTime, effectiveWeather);

        if (wasTransitioning && !stateMachine.isTransitioning())
        {
            events::weather::WeatherStateChangedNotification n;
            n.previousState = previousState;
            n.newState = stateMachine.getCurrentState();
            dispatcher.publish(n);
        }
    }

    glm::vec3 WeatherServiceImpl::queryCameraPosition()
    {
        try
        {
            auto& dispatcher = ::events::EventDispatcher::instance();
            auto camOpt = dispatcher.query(events::scene::GetPrimaryCameraQuery{});
            if (camOpt.has_value())
            {
                events::scene::GetWorldTransformQuery tq;
                tq.entity = camOpt.value();
                auto xformOpt = dispatcher.query(tq);
                if (xformOpt.has_value())
                    return xformOpt->position;
            }
        }
        catch (...) {}
        return cachedCameraPos;
    }

    weather::WeatherState WeatherServiceImpl::evaluateZones(const weather::WeatherState& globalWeather)
    {
        return zoneEvaluator.evaluate(cachedCameraPos, globalWeather);
    }

    void WeatherServiceImpl::publishZoneTransitions()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        for (const auto& t : zoneEvaluator.getTransitions())
        {
            if (t.entered)
            {
                events::weather::WeatherZoneEnteredNotification n;
                n.entityId = t.entityId;
                dispatcher.publish(n);
            }
            else
            {
                events::weather::WeatherZoneExitedNotification n;
                n.entityId = t.entityId;
                dispatcher.publish(n);
            }
        }
    }

    void WeatherServiceImpl::applyCloudWeather(const weather::WeatherState& ws)
    {
        try
        {
            auto& dispatcher = ::events::EventDispatcher::instance();
            auto settings = dispatcher.query(events::cloud::GetCloudSettingsQuery{});
            settings.globalCoverage = ws.cloudCoverage;
            settings.globalDensity = ws.cloudDensity;
            settings.cloudType = ws.cloudType;
            settings.windSpeed = ws.windSpeed;
            settings.windDirectionDeg = ws.windDirectionDeg;
            settings.cloudColorTint = ws.atmosphereTint;
            settings.ambientIntensity = ws.ambientLightMult;

            auto lightning = lightningGenerator.getOutput();
            if (lightning.flashIntensity > 0.0f)
            {
                settings.ambientIntensity += lightning.flashIntensity * 1.6f;
                settings.cloudColorTint = glm::mix(settings.cloudColorTint, glm::vec3(1.0f), lightning.flashIntensity * 0.5f);
            }

            events::cloud::ApplyCloudSettingsCommand cmd;
            cmd.settings = settings;
            dispatcher.execute(cmd);
        }
        catch (...) {}
    }

    void WeatherServiceImpl::applyAtmosphereWeather(const weather::WeatherState& ws)
    {
        try
        {
            auto& dispatcher = ::events::EventDispatcher::instance();
            auto settings = dispatcher.query(events::atmosphere::GetAtmosphereSettingsQuery{});

            if (!basesAtmosCaptured)
            {
                baseSunIrradiance = settings.sunIrradiance;
                baseAerialIntensity = settings.aerialIntensity;
                baseAmbientIntensity = settings.ambientIntensity;
                basesAtmosCaptured = true;
            }
            else if (lastWrittenAmbient >= 0.0f &&
                     std::abs(settings.ambientIntensity - lastWrittenAmbient) > 1e-6f)
            {
                // The live value is not what we wrote last tick, so an external writer changed it.
                // Adopt it as the new base instead of overwriting the user's edit on every frame.
                baseAmbientIntensity = settings.ambientIntensity;
            }

            settings.sunIrradiance = baseSunIrradiance * ws.atmosphereTint;
            settings.aerialIntensity = baseAerialIntensity * ws.ambientLightMult;
            // VK-1569: give ambientLightMult a true ambient meaning when dynamic ambient is on.
            settings.ambientIntensity = baseAmbientIntensity * ws.ambientLightMult;
            lastWrittenAmbient = settings.ambientIntensity;

            auto lightning = lightningGenerator.getOutput();
            if (lightning.flashIntensity > 0.0f)
                settings.sunIrradiance *= (1.0f + lightning.flashIntensity * 2.0f);

            events::atmosphere::ApplyAtmosphereSettingsCommand cmd;
            cmd.settings = settings;
            dispatcher.execute(cmd);
        }
        catch (...) {}
    }

    void WeatherServiceImpl::applyFogWeather(const weather::WeatherState& ws)
    {
        try
        {
            auto& dispatcher = ::events::EventDispatcher::instance();
            auto settings = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
            settings.volumetricFog.uniformDensity = ws.fogDensity;
            settings.volumetricFog.heightFogDensity = ws.heightFogDensity;
            settings.volumetricFog.fogColor[0] = ws.atmosphereTint.x;
            settings.volumetricFog.fogColor[1] = ws.atmosphereTint.y;
            settings.volumetricFog.fogColor[2] = ws.atmosphereTint.z;

            events::postprocess::ApplyPostProcessSettingsCommand cmd;
            cmd.settings = settings;
            dispatcher.execute(cmd);
        }
        catch (...) {}
    }

    void WeatherServiceImpl::applyWindWeather(const weather::WeatherState& ws)
    {
        try
        {
            auto& dispatcher = ::events::EventDispatcher::instance();
            auto config = dispatcher.query(events::vegetation::GetGlobalGrassConfigQuery{});
            float rad = ws.windDirectionDeg * static_cast<float>(M_PI) / 180.0f;
            config.windDirection = glm::vec3(std::cos(rad), 0.0f, std::sin(rad));
            config.windSpeed = ws.windSpeed;
            config.windStrength = std::clamp(ws.windSpeed / 40.0f, 0.0f, 1.0f);
            config.gustStrength = ws.gustStrength;
            config.gustFrequency = ws.gustFrequency;

            events::vegetation::SetGlobalGrassConfigCommand cmd;
            cmd.config = config;
            dispatcher.execute(cmd);
        }
        catch (...) {}
    }

    void WeatherServiceImpl::updateAccumulation(float deltaTime, const weather::WeatherState& state)
    {
        bool isRain = state.precipType == weather::PrecipitationType::Rain;
        bool isSnow = state.precipType == weather::PrecipitationType::Snow;

        weather::WeatherState zeroState = state;
        zeroState.precipIntensity = 0.0f;

        if (rainController) rainController->update(deltaTime, isRain ? state : zeroState);
        if (snowController) snowController->update(deltaTime, isSnow ? state : zeroState);

        if (vfxProvider && vfxProvider->isInitialized() &&
            ((rainController && rainController->isActive()) || (snowController && snowController->isActive())))
            vfxProvider->update(deltaTime);

        if (isSnow && state.precipIntensity > 0.01f)
            snowAccumulation = std::min(1.0f, snowAccumulation + deltaTime * state.precipIntensity * 0.05f);
        else
            snowAccumulation = std::max(0.0f, snowAccumulation - deltaTime * 0.02f);

        if (isRain && state.precipIntensity > 0.01f)
            wetness = std::min(1.0f, wetness + deltaTime * state.precipIntensity * 0.05f);
        else
            wetness = std::max(0.0f, wetness - deltaTime * 0.02f);

        try
        {
            auto& dispatcher = ::events::EventDispatcher::instance();
            events::weather::SetSnowAccumulationCommand snowCmd;
            snowCmd.accumulation = snowAccumulation;
            dispatcher.execute(snowCmd);

            events::weather::SetWetnessCommand wetCmd;
            wetCmd.wetness = wetness;
            dispatcher.execute(wetCmd);
        }
        catch (...) {}
    }

    void WeatherServiceImpl::updateScreenEffects(const weather::WeatherState& state)
    {
        bool isRain = state.precipType == weather::PrecipitationType::Rain;
        try
        {
            auto& dispatcher = ::events::EventDispatcher::instance();
            auto settings = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
            settings.rainDroplets.enabled = isRain && state.precipIntensity > 0.01f;
            settings.rainDroplets.intensity = isRain ? state.precipIntensity : 0.0f;

            events::postprocess::ApplyPostProcessSettingsCommand cmd;
            cmd.settings = settings;
            dispatcher.execute(cmd);
        }
        catch (...) {}
    }

    void WeatherServiceImpl::updateLightning(float deltaTime, const weather::WeatherState& state)
    {
        lightningGenerator.update(deltaTime, state, cachedCameraPos);
        auto lightning = lightningGenerator.getOutput();

        if (!lightning.shouldPlayThunder) return;

        const auto& thunderPaths = audioController.getConfig().thunderPaths;
        int idx = lightning.thunderSoundIndex % 3;

        if (!thunderPaths[idx].empty())
        {
            try
            {
                auto& dispatcher = ::events::EventDispatcher::instance();
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
        }

        try
        {
            auto& dispatcher = ::events::EventDispatcher::instance();
            events::weather::LightningStrikeNotification n;
            n.position = lightning.thunderPosition;
            n.intensity = 1.0f;
            dispatcher.publish(n);
        }
        catch (...) {}
    }

    void WeatherServiceImpl::cleanupEffects()
    {
        weather::WeatherState clearState{};
        clearState.precipIntensity = 0.0f;
        clearState.precipType = weather::PrecipitationType::None;

        if (rainController) rainController->update(0.0f, clearState);
        if (snowController) snowController->update(0.0f, clearState);

        audioController.update(0.0f, clearState);

        try
        {
            auto& dispatcher = ::events::EventDispatcher::instance();
            auto settings = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
            settings.rainDroplets.enabled = false;
            settings.rainDroplets.intensity = 0.0f;
            events::postprocess::ApplyPostProcessSettingsCommand cmd;
            cmd.settings = settings;
            dispatcher.execute(cmd);
        }
        catch (...) {}

        if (basesAtmosCaptured)
        {
            try
            {
                auto& dispatcher = ::events::EventDispatcher::instance();
                auto settings = dispatcher.query(events::atmosphere::GetAtmosphereSettingsQuery{});
                settings.sunIrradiance = baseSunIrradiance;
                settings.aerialIntensity = baseAerialIntensity;
                settings.ambientIntensity = baseAmbientIntensity;
                events::atmosphere::ApplyAtmosphereSettingsCommand cmd;
                cmd.settings = settings;
                dispatcher.execute(cmd);
            }
            catch (...) {}
            basesAtmosCaptured = false;
        }

        try
        {
            auto& dispatcher = ::events::EventDispatcher::instance();
            auto settings = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
            settings.volumetricFog.uniformDensity = 0.0f;
            settings.volumetricFog.heightFogDensity = 0.0f;
            events::postprocess::ApplyPostProcessSettingsCommand cmd;
            cmd.settings = settings;
            dispatcher.execute(cmd);
        }
        catch (...) {}

        try
        {
            auto& dispatcher = ::events::EventDispatcher::instance();
            auto settings = dispatcher.query(events::cloud::GetCloudSettingsQuery{});
            settings.globalCoverage = 0.0f;
            settings.globalDensity = 0.0f;
            settings.windSpeed = 0.0f;
            settings.ambientIntensity = 1.0f;
            settings.cloudColorTint = glm::vec3(1.0f);
            events::cloud::ApplyCloudSettingsCommand cmd;
            cmd.settings = settings;
            dispatcher.execute(cmd);
        }
        catch (...) {}

        try
        {
            auto& dispatcher = ::events::EventDispatcher::instance();
            auto config = dispatcher.query(events::vegetation::GetGlobalGrassConfigQuery{});
            config.windSpeed = 0.0f;
            config.windStrength = 0.0f;
            config.gustStrength = 0.0f;
            config.gustFrequency = 0.0f;
            events::vegetation::SetGlobalGrassConfigCommand cmd;
            cmd.config = config;
            dispatcher.execute(cmd);
        }
        catch (...) {}

        snowAccumulation = 0.0f;
        wetness = 0.0f;

        try
        {
            auto& dispatcher = ::events::EventDispatcher::instance();
            events::weather::SetSnowAccumulationCommand snowCmd;
            snowCmd.accumulation = 0.0f;
            dispatcher.execute(snowCmd);
            events::weather::SetWetnessCommand wetCmd;
            wetCmd.wetness = 0.0f;
            dispatcher.execute(wetCmd);
        }
        catch (...) {}

        stateMachine.setImmediate(weather::WeatherState{});
    }
}

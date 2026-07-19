#pragma once

#include "../../interfaces/weather/IWeatherService.hpp"
#include "PrecipitationController.hpp"
#include "weather/WeatherStateMachine.hpp"
#include "weather/WeatherSchedule.hpp"
#include "weather/LightningGenerator.hpp"
#include "weather/WeatherZoneEvaluator.hpp"
#include "weather/WeatherAudioController.hpp"
#include <memory>

namespace services
{
    class IVFXRuntimeProvider;
    class RainController;
    class SnowController;

    class WeatherServiceImpl : public IWeatherService
    {
    public:
        explicit WeatherServiceImpl(IVFXRuntimeProvider* vfxProvider = nullptr);
        ~WeatherServiceImpl() override;

        void registerEventHandlers() override;

    private:
        void onUpdate(float deltaTime);

        void registerWeatherCommands();
        void registerWeatherQueries();

        glm::vec3 queryCameraPosition();
        weather::WeatherState evaluateZones(const weather::WeatherState& globalWeather);
        void publishZoneTransitions();

        void applyCloudWeather(const weather::WeatherState& ws);
        void applyAtmosphereWeather(const weather::WeatherState& ws);
        void applyFogWeather(const weather::WeatherState& ws);
        void applyWindWeather(const weather::WeatherState& ws);

        void updateAccumulation(float deltaTime, const weather::WeatherState& state);
        void updateScreenEffects(const weather::WeatherState& state);
        void updateLightning(float deltaTime, const weather::WeatherState& state);
        void cleanupEffects();

        weather::WeatherStateMachine stateMachine;
        weather::WeatherSchedule schedule;
        bool weatherEnabled = false;
        bool scheduleEnabled = false;
        float snowAccumulation = 0.0f;
        float wetness = 0.0f;

        glm::vec3 baseSunIrradiance{0.0f};
        float baseAerialIntensity = 0.0f;
        float baseAmbientIntensity = 0.0f; // VK-1569: base for dynamic-ambient weather multiplier
        bool basesAtmosCaptured = false;

        IVFXRuntimeProvider* vfxProvider = nullptr;
        std::unique_ptr<PrecipitationController> rainController;
        std::unique_ptr<PrecipitationController> snowController;
        weather::LightningGenerator lightningGenerator;
        weather::WeatherZoneEvaluator zoneEvaluator;
        weather::WeatherAudioController audioController;
        glm::vec3 cachedCameraPos{0.0f};
    };
}

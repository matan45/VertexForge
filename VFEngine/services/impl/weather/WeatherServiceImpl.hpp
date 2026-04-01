#pragma once

#pragma once

#include "../../interfaces/weather/IWeatherService.hpp"
#include "RainController.hpp"
#include "SnowController.hpp"
#include "weather/WeatherStateMachine.hpp"
#include "weather/WeatherSchedule.hpp"
#include "weather/LightningGenerator.hpp"
#include "weather/WeatherZoneEvaluator.hpp"
#include "weather/WeatherAudioController.hpp"
#include <memory>

namespace services
{
    class IVFXRuntimeProvider;

    class WeatherServiceImpl : public IWeatherService
    {
    public:
        explicit WeatherServiceImpl(IVFXRuntimeProvider* vfxProvider = nullptr);
        ~WeatherServiceImpl() override;

        void registerEventHandlers() override;

    private:
        void onUpdate(float deltaTime);
        void applyWeatherToPipelines(const weather::WeatherState& state);
        void updatePrecipitation(float deltaTime, const weather::WeatherState& state);

        weather::WeatherStateMachine stateMachine;
        weather::WeatherSchedule schedule;
        bool weatherEnabled = false;
        bool scheduleEnabled = false;
        float snowAccumulation = 0.0f;

        // Base atmosphere values captured before weather modifies them
        glm::vec3 baseSunIrradiance{0.0f};
        float baseAerialIntensity = 0.0f;
        bool basesAtmosCaptured = false;

        IVFXRuntimeProvider* vfxProvider = nullptr;
        std::unique_ptr<RainController> rainController;
        std::unique_ptr<SnowController> snowController;
        weather::LightningGenerator lightningGenerator;
        weather::WeatherZoneEvaluator zoneEvaluator;
        weather::WeatherAudioController audioController;
        glm::vec3 cachedCameraPos{0.0f};
    };
}

#pragma once

#include "../../interfaces/weather/IWeatherService.hpp"
#include "RainController.hpp"
#include "weather/WeatherStateMachine.hpp"
#include "weather/WeatherSchedule.hpp"
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

        weather::WeatherStateMachine stateMachine;
        weather::WeatherSchedule schedule;
        bool weatherEnabled = false;
        bool scheduleEnabled = false;

        std::unique_ptr<RainController> rainController;
    };
}

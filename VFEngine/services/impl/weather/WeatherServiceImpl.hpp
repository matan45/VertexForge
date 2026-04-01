#pragma once

#include "../../interfaces/weather/IWeatherService.hpp"
#include "weather/WeatherStateMachine.hpp"
#include "weather/WeatherSchedule.hpp"

namespace services
{
    class WeatherServiceImpl : public IWeatherService
    {
    public:
        WeatherServiceImpl() = default;
        ~WeatherServiceImpl() override = default;

        void registerEventHandlers() override;

    private:
        void onUpdate(float deltaTime);
        void applyWeatherToPipelines(const weather::WeatherState& state);

        weather::WeatherStateMachine stateMachine;
        weather::WeatherSchedule schedule;
        bool weatherEnabled = false;
        bool scheduleEnabled = false;
        bool wasTransitioning = false;
        weather::WeatherState lastCompletedState;
    };
}

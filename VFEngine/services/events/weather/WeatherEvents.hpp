#pragma once

#include "../EventTypes.hpp"
#include "weather/WeatherTypes.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace events::weather
{
    // --- Commands ---

    struct UpdateWeatherCommand : ICommand<>
    {
        float deltaTime = 0.0f;
        std::string_view getName() const override { return "UpdateWeather"; }
    };

    struct SetWeatherPresetCommand : ICommand<>
    {
        ::weather::WeatherPresetId preset = ::weather::WeatherPresetId::Clear;
        float transitionDuration = 60.0f;
        ::weather::WeatherEasing easing = ::weather::WeatherEasing::EaseInOut;
        std::string_view getName() const override { return "SetWeatherPreset"; }
    };

    struct SetWeatherStateCommand : ICommand<>
    {
        ::weather::WeatherState state;
        float transitionDuration = 60.0f;
        ::weather::WeatherEasing easing = ::weather::WeatherEasing::EaseInOut;
        std::string_view getName() const override { return "SetWeatherState"; }
    };

    struct SetWeatherImmediateCommand : ICommand<>
    {
        ::weather::WeatherState state;
        std::string_view getName() const override { return "SetWeatherImmediate"; }
    };

    struct QueueWeatherCommand : ICommand<>
    {
        ::weather::WeatherState state;
        float transitionDuration = 60.0f;
        ::weather::WeatherEasing easing = ::weather::WeatherEasing::EaseInOut;
        std::string_view getName() const override { return "QueueWeather"; }
    };

    struct SetWeatherScheduleCommand : ICommand<>
    {
        nlohmann::json scheduleJson;
        std::string_view getName() const override { return "SetWeatherSchedule"; }
    };

    struct SetWeatherScheduleEnabledCommand : ICommand<>
    {
        bool enabled = false;
        std::string_view getName() const override { return "SetWeatherScheduleEnabled"; }
    };

    struct SetWeatherBiomeCommand : ICommand<>
    {
        std::string biomeId;
        std::string_view getName() const override { return "SetWeatherBiome"; }
    };

    struct SetWeatherEnabledCommand : ICommand<>
    {
        bool enabled = false;
        std::string_view getName() const override { return "SetWeatherEnabled"; }
    };

    // --- Queries ---

    struct GetWeatherStateQuery : IQuery<::weather::WeatherState>
    {
        std::string_view getName() const override { return "GetWeatherState"; }
    };

    struct GetWeatherTransitionProgressQuery : IQuery<float>
    {
        std::string_view getName() const override { return "GetWeatherTransitionProgress"; }
    };

    struct IsWeatherEnabledQuery : IQuery<bool>
    {
        std::string_view getName() const override { return "IsWeatherEnabled"; }
    };

    struct IsWeatherScheduleEnabledQuery : IQuery<bool>
    {
        std::string_view getName() const override { return "IsWeatherScheduleEnabled"; }
    };

    struct SetSnowAccumulationCommand : ICommand<>
    {
        float accumulation = 0.0f;
        std::string_view getName() const override { return "SetSnowAccumulation"; }
    };

    struct GetSnowAccumulationQuery : IQuery<float>
    {
        std::string_view getName() const override { return "GetSnowAccumulation"; }
    };

    // --- Notifications ---

    struct WeatherStateChangedNotification : INotification
    {
        ::weather::WeatherState previousState;
        ::weather::WeatherState newState;
        std::string_view getName() const override { return "WeatherStateChanged"; }
    };

    struct LightningStrikeNotification : INotification
    {
        glm::vec3 position{0.0f};
        float intensity = 1.0f;
        std::string_view getName() const override { return "LightningStrike"; }
    };
}

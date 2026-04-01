#pragma once

#include "WeatherTypes.hpp"
#include <array>

namespace weather
{
    namespace presets
    {
        // Clear sky - minimal clouds, calm wind
        inline constexpr WeatherState Clear{
            .cloudCoverage = 0.1f, .cloudDensity = 0.2f, .cloudType = 0.7f,
            .precipIntensity = 0.0f,
            .windSpeed = 3.0f, .windDirectionDeg = 45.0f, .gustStrength = 0.05f, .gustFrequency = 0.2f,
            .fogDensity = 0.0f, .heightFogDensity = 0.0f,
            .atmosphereTint = {1.0f, 1.0f, 1.0f}, .ambientLightMult = 1.0f
        };

        // Partly cloudy - scattered clouds
        inline constexpr WeatherState Cloudy{
            .cloudCoverage = 0.45f, .cloudDensity = 0.4f, .cloudType = 0.6f,
            .precipIntensity = 0.0f,
            .windSpeed = 8.0f, .windDirectionDeg = 60.0f, .gustStrength = 0.15f, .gustFrequency = 0.3f,
            .fogDensity = 0.005f, .heightFogDensity = 0.01f,
            .atmosphereTint = {0.95f, 0.95f, 0.97f}, .ambientLightMult = 0.9f
        };

        // Full overcast - thick cloud layer
        inline constexpr WeatherState Overcast{
            .cloudCoverage = 0.85f, .cloudDensity = 0.7f, .cloudType = 0.2f,
            .precipIntensity = 0.0f,
            .windSpeed = 10.0f, .windDirectionDeg = 90.0f, .gustStrength = 0.2f, .gustFrequency = 0.4f,
            .fogDensity = 0.01f, .heightFogDensity = 0.02f,
            .atmosphereTint = {0.85f, 0.85f, 0.88f}, .ambientLightMult = 0.7f
        };

        // Light rain - moderate clouds, light precipitation
        inline constexpr WeatherState LightRain{
            .cloudCoverage = 0.75f, .cloudDensity = 0.65f, .cloudType = 0.3f,
            .precipIntensity = 0.3f,
            .windSpeed = 12.0f, .windDirectionDeg = 120.0f, .gustStrength = 0.25f, .gustFrequency = 0.5f,
            .fogDensity = 0.015f, .heightFogDensity = 0.03f,
            .atmosphereTint = {0.8f, 0.82f, 0.85f}, .ambientLightMult = 0.65f
        };

        // Heavy rain - dense clouds, heavy precipitation
        inline constexpr WeatherState HeavyRain{
            .cloudCoverage = 0.92f, .cloudDensity = 0.85f, .cloudType = 0.15f,
            .precipIntensity = 0.8f,
            .windSpeed = 20.0f, .windDirectionDeg = 150.0f, .gustStrength = 0.5f, .gustFrequency = 0.7f,
            .fogDensity = 0.03f, .heightFogDensity = 0.05f,
            .atmosphereTint = {0.65f, 0.67f, 0.72f}, .ambientLightMult = 0.5f
        };

        // Thunderstorm - maximum cloud coverage, intense rain and wind
        inline constexpr WeatherState Thunderstorm{
            .cloudCoverage = 0.98f, .cloudDensity = 0.95f, .cloudType = 0.1f,
            .precipIntensity = 1.0f,
            .windSpeed = 35.0f, .windDirectionDeg = 180.0f, .gustStrength = 0.8f, .gustFrequency = 0.9f,
            .fogDensity = 0.04f, .heightFogDensity = 0.06f,
            .atmosphereTint = {0.5f, 0.5f, 0.55f}, .ambientLightMult = 0.35f
        };

        // Light snow - moderate clouds, gentle snowfall
        inline constexpr WeatherState LightSnow{
            .cloudCoverage = 0.7f, .cloudDensity = 0.6f, .cloudType = 0.25f,
            .precipIntensity = 0.25f,
            .windSpeed = 6.0f, .windDirectionDeg = 30.0f, .gustStrength = 0.15f, .gustFrequency = 0.3f,
            .fogDensity = 0.02f, .heightFogDensity = 0.03f,
            .atmosphereTint = {0.9f, 0.92f, 0.95f}, .ambientLightMult = 0.75f
        };

        // Heavy snow - blizzard conditions
        inline constexpr WeatherState HeavySnow{
            .cloudCoverage = 0.9f, .cloudDensity = 0.8f, .cloudType = 0.15f,
            .precipIntensity = 0.7f,
            .windSpeed = 25.0f, .windDirectionDeg = 0.0f, .gustStrength = 0.6f, .gustFrequency = 0.8f,
            .fogDensity = 0.06f, .heightFogDensity = 0.08f,
            .atmosphereTint = {0.85f, 0.87f, 0.92f}, .ambientLightMult = 0.55f
        };

        // Fog - low visibility, calm conditions
        inline constexpr WeatherState Fog{
            .cloudCoverage = 0.5f, .cloudDensity = 0.4f, .cloudType = 0.1f,
            .precipIntensity = 0.0f,
            .windSpeed = 2.0f, .windDirectionDeg = 0.0f, .gustStrength = 0.05f, .gustFrequency = 0.1f,
            .fogDensity = 0.1f, .heightFogDensity = 0.15f,
            .atmosphereTint = {0.88f, 0.9f, 0.92f}, .ambientLightMult = 0.6f
        };

        // Sandstorm - dust-tinted atmosphere, strong wind
        inline constexpr WeatherState Sandstorm{
            .cloudCoverage = 0.3f, .cloudDensity = 0.25f, .cloudType = 0.5f,
            .precipIntensity = 0.0f,
            .windSpeed = 40.0f, .windDirectionDeg = 220.0f, .gustStrength = 0.9f, .gustFrequency = 1.0f,
            .fogDensity = 0.08f, .heightFogDensity = 0.1f,
            .atmosphereTint = {0.9f, 0.75f, 0.5f}, .ambientLightMult = 0.45f
        };
    }

    inline const WeatherState& getPreset(WeatherPresetId id)
    {
        static const std::array<WeatherState, 10> presetTable = {
            presets::Clear,
            presets::Cloudy,
            presets::Overcast,
            presets::LightRain,
            presets::HeavyRain,
            presets::Thunderstorm,
            presets::LightSnow,
            presets::HeavySnow,
            presets::Fog,
            presets::Sandstorm
        };

        auto idx = static_cast<size_t>(id);
        if (idx >= presetTable.size())
            return presetTable[0]; // fallback to Clear

        return presetTable[idx];
    }
}

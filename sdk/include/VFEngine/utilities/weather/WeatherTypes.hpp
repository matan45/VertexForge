#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <optional>
#include <cmath>

namespace weather
{
    enum class WeatherPresetId : uint8_t
    {
        Clear,
        Cloudy,
        Overcast,
        LightRain,
        HeavyRain,
        Thunderstorm,
        LightSnow,
        HeavySnow,
        Fog,
        Sandstorm,
        Custom
    };

    enum class WeatherEasing : uint8_t
    {
        Linear,
        EaseInOut
    };

    enum class PrecipitationType : uint8_t
    {
        None,
        Rain,
        Snow
    };

    struct WeatherState
    {
        // Cloud parameters
        float cloudCoverage = 0.1f;     // [0,1] maps to CloudSettings::globalCoverage
        float cloudDensity = 0.3f;      // [0,1] maps to CloudSettings::globalDensity
        float cloudType = 0.7f;         // [0,1] stratus(0) to cumulus(1)

        // Precipitation
        PrecipitationType precipType = PrecipitationType::None;
        float precipIntensity = 0.0f;   // [0,1]

        // Wind
        float windSpeed = 5.0f;         // m/s
        float windDirectionDeg = 45.0f; // degrees
        float gustStrength = 0.1f;      // [0,1]
        float gustFrequency = 0.3f;     // gusts per second

        // Fog
        float fogDensity = 0.0f;        // maps to VolumetricFogSettings::uniformDensity
        float heightFogDensity = 0.0f;  // maps to VolumetricFogSettings::heightFogDensity

        // Atmosphere
        glm::vec3 atmosphereTint{1.0f, 1.0f, 1.0f};  // color multiplier
        float ambientLightMult = 1.0f;                 // multiplier on ambient lighting
    };

    struct WeatherTransition
    {
        WeatherState targetState;
        float duration = 60.0f;
        WeatherEasing easing = WeatherEasing::EaseInOut;
    };


    inline WeatherState lerpWeatherState(const WeatherState& a, const WeatherState& b, float t)
    {
        WeatherState result;
        result.cloudCoverage = glm::mix(a.cloudCoverage, b.cloudCoverage, t);
        result.cloudDensity = glm::mix(a.cloudDensity, b.cloudDensity, t);
        result.cloudType = glm::mix(a.cloudType, b.cloudType, t);
        result.precipType = (t < 0.5f) ? a.precipType : b.precipType;
        result.precipIntensity = glm::mix(a.precipIntensity, b.precipIntensity, t);
        result.windSpeed = glm::mix(a.windSpeed, b.windSpeed, t);
        float windDelta = b.windDirectionDeg - a.windDirectionDeg;
        windDelta = windDelta - 360.0f * std::floor((windDelta + 180.0f) / 360.0f);
        result.windDirectionDeg = a.windDirectionDeg + windDelta * t;
        result.gustStrength = glm::mix(a.gustStrength, b.gustStrength, t);
        result.gustFrequency = glm::mix(a.gustFrequency, b.gustFrequency, t);
        result.fogDensity = glm::mix(a.fogDensity, b.fogDensity, t);
        result.heightFogDensity = glm::mix(a.heightFogDensity, b.heightFogDensity, t);
        result.atmosphereTint = glm::mix(a.atmosphereTint, b.atmosphereTint, t);
        result.ambientLightMult = glm::mix(a.ambientLightMult, b.ambientLightMult, t);
        return result;
    }

    inline std::optional<WeatherPresetId> stringToPresetId(const std::string& name)
    {
        if (name == "Clear")        return WeatherPresetId::Clear;
        if (name == "Cloudy")       return WeatherPresetId::Cloudy;
        if (name == "Overcast")     return WeatherPresetId::Overcast;
        if (name == "LightRain")    return WeatherPresetId::LightRain;
        if (name == "HeavyRain")    return WeatherPresetId::HeavyRain;
        if (name == "Thunderstorm") return WeatherPresetId::Thunderstorm;
        if (name == "LightSnow")    return WeatherPresetId::LightSnow;
        if (name == "HeavySnow")    return WeatherPresetId::HeavySnow;
        if (name == "Fog")          return WeatherPresetId::Fog;
        if (name == "Sandstorm")    return WeatherPresetId::Sandstorm;
        return std::nullopt;
    }

    inline const char* presetIdToString(WeatherPresetId id)
    {
        switch (id)
        {
        case WeatherPresetId::Clear:        return "Clear";
        case WeatherPresetId::Cloudy:       return "Cloudy";
        case WeatherPresetId::Overcast:     return "Overcast";
        case WeatherPresetId::LightRain:    return "LightRain";
        case WeatherPresetId::HeavyRain:    return "HeavyRain";
        case WeatherPresetId::Thunderstorm: return "Thunderstorm";
        case WeatherPresetId::LightSnow:    return "LightSnow";
        case WeatherPresetId::HeavySnow:    return "HeavySnow";
        case WeatherPresetId::Fog:          return "Fog";
        case WeatherPresetId::Sandstorm:    return "Sandstorm";
        default:                            return "Custom";
        }
    }
}

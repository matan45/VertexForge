#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>

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

    inline void to_json(nlohmann::json& j, const WeatherState& s)
    {
        j = nlohmann::json{
            {"cloudCoverage", s.cloudCoverage},
            {"cloudDensity", s.cloudDensity},
            {"cloudType", s.cloudType},
            {"precipType", static_cast<uint8_t>(s.precipType)},
            {"precipIntensity", s.precipIntensity},
            {"windSpeed", s.windSpeed},
            {"windDirectionDeg", s.windDirectionDeg},
            {"gustStrength", s.gustStrength},
            {"gustFrequency", s.gustFrequency},
            {"fogDensity", s.fogDensity},
            {"heightFogDensity", s.heightFogDensity},
            {"atmosphereTint", {s.atmosphereTint.x, s.atmosphereTint.y, s.atmosphereTint.z}},
            {"ambientLightMult", s.ambientLightMult}
        };
    }

    inline void from_json(const nlohmann::json& j, WeatherState& s)
    {
        if (j.contains("cloudCoverage") && j["cloudCoverage"].is_number())
            s.cloudCoverage = j["cloudCoverage"].get<float>();
        if (j.contains("cloudDensity") && j["cloudDensity"].is_number())
            s.cloudDensity = j["cloudDensity"].get<float>();
        if (j.contains("cloudType") && j["cloudType"].is_number())
            s.cloudType = j["cloudType"].get<float>();
        if (j.contains("precipType") && j["precipType"].is_number_unsigned())
            s.precipType = static_cast<PrecipitationType>(j["precipType"].get<uint8_t>());
        if (j.contains("precipIntensity") && j["precipIntensity"].is_number())
            s.precipIntensity = j["precipIntensity"].get<float>();
        if (j.contains("windSpeed") && j["windSpeed"].is_number())
            s.windSpeed = j["windSpeed"].get<float>();
        if (j.contains("windDirectionDeg") && j["windDirectionDeg"].is_number())
            s.windDirectionDeg = j["windDirectionDeg"].get<float>();
        if (j.contains("gustStrength") && j["gustStrength"].is_number())
            s.gustStrength = j["gustStrength"].get<float>();
        if (j.contains("gustFrequency") && j["gustFrequency"].is_number())
            s.gustFrequency = j["gustFrequency"].get<float>();
        if (j.contains("fogDensity") && j["fogDensity"].is_number())
            s.fogDensity = j["fogDensity"].get<float>();
        if (j.contains("heightFogDensity") && j["heightFogDensity"].is_number())
            s.heightFogDensity = j["heightFogDensity"].get<float>();
        if (j.contains("atmosphereTint") && j["atmosphereTint"].is_array() && j["atmosphereTint"].size() == 3)
        {
            s.atmosphereTint.x = j["atmosphereTint"][0].get<float>();
            s.atmosphereTint.y = j["atmosphereTint"][1].get<float>();
            s.atmosphereTint.z = j["atmosphereTint"][2].get<float>();
        }
        if (j.contains("ambientLightMult") && j["ambientLightMult"].is_number())
            s.ambientLightMult = j["ambientLightMult"].get<float>();
    }

    inline WeatherState lerpWeatherState(const WeatherState& a, const WeatherState& b, float t)
    {
        WeatherState result;
        result.cloudCoverage = glm::mix(a.cloudCoverage, b.cloudCoverage, t);
        result.cloudDensity = glm::mix(a.cloudDensity, b.cloudDensity, t);
        result.cloudType = glm::mix(a.cloudType, b.cloudType, t);
        result.precipType = (t < 0.5f) ? a.precipType : b.precipType;
        result.precipIntensity = glm::mix(a.precipIntensity, b.precipIntensity, t);
        result.windSpeed = glm::mix(a.windSpeed, b.windSpeed, t);
        result.windDirectionDeg = glm::mix(a.windDirectionDeg, b.windDirectionDeg, t);
        result.gustStrength = glm::mix(a.gustStrength, b.gustStrength, t);
        result.gustFrequency = glm::mix(a.gustFrequency, b.gustFrequency, t);
        result.fogDensity = glm::mix(a.fogDensity, b.fogDensity, t);
        result.heightFogDensity = glm::mix(a.heightFogDensity, b.heightFogDensity, t);
        result.atmosphereTint = glm::mix(a.atmosphereTint, b.atmosphereTint, t);
        result.ambientLightMult = glm::mix(a.ambientLightMult, b.ambientLightMult, t);
        return result;
    }
}

#pragma once

#include "WeatherTypes.hpp"
#include <nlohmann/json.hpp>

namespace weather
{
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
}

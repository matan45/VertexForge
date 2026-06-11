#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <cmath>
#include "../weather/WeatherTypes.hpp"

namespace water
{
    inline constexpr uint32_t SEA_STATE_BANDS = 3;

    // Per-band FFT parameters a sea state drives. Deliberately excludes resolution and
    // patchSize: changing those tears down and rebuilds the ocean GPU resources (waitIdle
    // path in OceanFFT), which must never happen from a per-frame weather tick.
    struct SeaStateBandParams
    {
        float windSpeed = 8.0f;
        float windDirection = 45.0f;
        float amplitude = 0.00003f;
        float choppiness = 1.2f;
        float displacementScale = 4.0f;
        float foamThreshold = -0.1f;
    };

    struct SeaState
    {
        SeaStateBandParams bands[SEA_STATE_BANDS];
    };

    enum class SeaStatePreset : uint8_t
    {
        Calm = 0,
        Slight,
        Moderate,
        Rough,
        VeryRough,
        Storm
    };

    inline float beaufortForPreset(SeaStatePreset preset)
    {
        switch (preset)
        {
        case SeaStatePreset::Calm: return 1.0f;
        case SeaStatePreset::Slight: return 3.0f;
        case SeaStatePreset::Moderate: return 5.0f;
        case SeaStatePreset::Rough: return 7.0f;
        case SeaStatePreset::VeryRough: return 9.0f;
        case SeaStatePreset::Storm: return 12.0f;
        }
        return 3.0f;
    }

    inline const char* seaStatePresetName(SeaStatePreset preset)
    {
        switch (preset)
        {
        case SeaStatePreset::Calm: return "Calm";
        case SeaStatePreset::Slight: return "Slight";
        case SeaStatePreset::Moderate: return "Moderate";
        case SeaStatePreset::Rough: return "Rough";
        case SeaStatePreset::VeryRough: return "Very Rough";
        case SeaStatePreset::Storm: return "Storm";
        }
        return "Custom";
    }

    // Standard Beaufort relation: v = 0.836 * B^1.5 (m/s)
    inline float windSpeedFromBeaufort(float beaufort)
    {
        float bf = glm::max(beaufort, 0.0f);
        return 0.836f * std::pow(bf, 1.5f);
    }

    inline float beaufortFromWindSpeed(float metersPerSecond)
    {
        float ms = glm::max(metersPerSecond, 0.0f);
        return std::pow(ms / 0.836f, 2.0f / 3.0f);
    }

    // Sea state for a Beaufort number, anchored so Beaufort 6 reproduces the
    // OceanComponent band defaults (12 m/s swell wind). Wave energy scales
    // quadratically with Beaufort; choppiness and foam onset rise toward storms.
    inline SeaState seaStateFromBeaufort(float beaufort, float windDirectionDeg)
    {
        constexpr float anchorBeaufort = 6.0f;
        constexpr float baseAmplitude[SEA_STATE_BANDS] = {0.00005f, 0.00003f, 0.00001f};
        constexpr float baseChoppiness[SEA_STATE_BANDS] = {1.5f, 1.2f, 0.8f};
        constexpr float bandWindFraction[SEA_STATE_BANDS] = {1.0f, 0.65f, 0.35f};
        constexpr float bandDirectionOffset[SEA_STATE_BANDS] = {0.0f, 15.0f, -15.0f};

        float bf = glm::clamp(beaufort, 0.0f, 12.0f);
        float energy = bf / anchorBeaufort;
        float amplitudeScale = glm::max(energy * energy, 0.0025f);
        float choppinessScale = 0.6f + 0.4f * energy;
        float foamThreshold = glm::mix(-0.25f, 0.05f, bf / 12.0f);
        float windSpeed = windSpeedFromBeaufort(bf);

        SeaState state;
        for (uint32_t i = 0; i < SEA_STATE_BANDS; ++i)
        {
            state.bands[i].windSpeed = glm::max(windSpeed * bandWindFraction[i], 0.5f);
            state.bands[i].windDirection = windDirectionDeg + bandDirectionOffset[i];
            state.bands[i].amplitude = baseAmplitude[i] * amplitudeScale;
            state.bands[i].choppiness = glm::clamp(baseChoppiness[i] * choppinessScale, 0.0f, 2.5f);
            state.bands[i].displacementScale = 4.0f;
            state.bands[i].foamThreshold = foamThreshold;
        }
        return state;
    }

    inline float lerpAngleDeg(float a, float b, float t)
    {
        float delta = b - a;
        delta = delta - 360.0f * std::floor((delta + 180.0f) / 360.0f);
        return a + delta * t;
    }

    inline SeaStateBandParams lerpSeaStateBand(const SeaStateBandParams& a,
                                               const SeaStateBandParams& b, float t)
    {
        SeaStateBandParams result;
        result.windSpeed = glm::mix(a.windSpeed, b.windSpeed, t);
        result.windDirection = lerpAngleDeg(a.windDirection, b.windDirection, t);
        result.amplitude = glm::mix(a.amplitude, b.amplitude, t);
        result.choppiness = glm::mix(a.choppiness, b.choppiness, t);
        result.displacementScale = glm::mix(a.displacementScale, b.displacementScale, t);
        result.foamThreshold = glm::mix(a.foamThreshold, b.foamThreshold, t);
        return result;
    }

    inline SeaState lerpSeaState(const SeaState& a, const SeaState& b, float t)
    {
        SeaState result;
        for (uint32_t i = 0; i < SEA_STATE_BANDS; ++i)
            result.bands[i] = lerpSeaStateBand(a.bands[i], b.bands[i], t);
        return result;
    }

    // Gusts add up to one extra Beaufort; response scales how strongly the ocean follows
    // the weather (1 = direct mapping).
    inline float beaufortFromWeather(const weather::WeatherState& ws, float response)
    {
        return glm::clamp((beaufortFromWindSpeed(ws.windSpeed) + ws.gustStrength) * response,
                          0.0f, 12.0f);
    }

    inline SeaState mapWeatherToSeaState(const weather::WeatherState& ws, float response)
    {
        return seaStateFromBeaufort(beaufortFromWeather(ws, response), ws.windDirectionDeg);
    }

    // Quantize the weather inputs before mapping so a slow 60 s weather transition does
    // not bump the ocean config version every frame (each bump re-dispatches the spectrum).
    inline float quantizeBeaufort(float beaufort, float step = 0.1f)
    {
        return std::round(beaufort / step) * step;
    }

    inline float quantizeDirectionDeg(float degrees, float step = 2.0f)
    {
        return std::round(degrees / step) * step;
    }
}

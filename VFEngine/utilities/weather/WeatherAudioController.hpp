#pragma once

#include "WeatherTypes.hpp"
#include <cstdint>

namespace weather
{
    class WeatherAudioController
    {
    public:
        void init();
        void cleanup();
        void update(float deltaTime, const WeatherState& state);

    private:
        void updateWindAmbient(float deltaTime, const WeatherState& state);
        void updateRainAmbient(float deltaTime, const WeatherState& state);
        void updateSnowAmbient(float deltaTime, const WeatherState& state);

        void startLoop(uint64_t& handle, const char* path, float volume);
        void fadeOutLoop(uint64_t& handle);
        void setLoopVolume(uint64_t handle, float volume);
        void setLoopPitch(uint64_t handle, float pitch);

        uint64_t rainHandle = 0;
        uint64_t windHandle = 0;
        uint64_t snowHandle = 0;

        float rainVolume = 0.0f;
        float windVolume = 0.0f;
        float snowVolume = 0.0f;

        PrecipitationType activePrecipType = PrecipitationType::None;
        bool initialized = false;

        static constexpr float VOLUME_RAMP_SPEED = 1.5f;
        static constexpr float CROSSFADE_DURATION_MS = 2000.0f;
        static constexpr float WIND_BASE_VOLUME = 0.1f;
        static constexpr float WIND_MAX_VOLUME = 0.6f;
        static constexpr float RAIN_MAX_VOLUME = 0.7f;
        static constexpr float SNOW_MAX_VOLUME = 0.3f;
    };
}

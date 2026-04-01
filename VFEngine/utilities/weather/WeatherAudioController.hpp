#pragma once

#include "WeatherTypes.hpp"
#include <cstdint>
#include <string>

namespace weather
{
    struct WeatherAudioConfig
    {
        std::string windLoopPath;
        std::string rainLoopPath;
        std::string snowLoopPath;
        std::string thunderPaths[3];
    };

    class WeatherAudioController
    {
    public:
        void init();
        void cleanup();
        void update(float deltaTime, const WeatherState& state);

        void setConfig(const WeatherAudioConfig& config) { audioConfig = config; }
        const WeatherAudioConfig& getConfig() const { return audioConfig; }

    private:
        void updateWindAmbient(float deltaTime, const WeatherState& state);
        void updateRainAmbient(float deltaTime, const WeatherState& state);
        void updateSnowAmbient(float deltaTime, const WeatherState& state);

        void startLoop(uint64_t& handle, const std::string& path, float volume);
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

        WeatherAudioConfig audioConfig;

        static constexpr float VOLUME_RAMP_SPEED = 1.5f;
        static constexpr float CROSSFADE_DURATION_MS = 2000.0f;
        static constexpr float WIND_BASE_VOLUME = 0.1f;
        static constexpr float WIND_MAX_VOLUME = 0.6f;
        static constexpr float RAIN_MAX_VOLUME = 0.7f;
        static constexpr float SNOW_MAX_VOLUME = 0.3f;
    };
}

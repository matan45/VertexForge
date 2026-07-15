#pragma once

#include "WeatherTypes.hpp"
#include "WeatherLoopGate.hpp"
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
        void updateLoop(float deltaTime, float targetVolume,
                        uint64_t& handle, float& volume, float& silentTime,
                        const std::string& path);
        // VK-1521: fadeInMs hands the onset to the engine's streaming fade. Callers pass
        // CROSSFADE_DURATION_MS so a loop's fade-in and fade-out are symmetric — which is
        // what makes a rain -> snow switch an actual crossfade rather than a cut plus a ramp.
        void startLoop(uint64_t& handle, const std::string& path, float volume, float fadeInMs);
        void fadeOutLoop(uint64_t& handle);
        void setLoopVolume(uint64_t handle, float volume);
        void setLoopPitch(uint64_t handle, float pitch);

        uint64_t rainHandle = 0;
        uint64_t windHandle = 0;
        uint64_t snowHandle = 0;

        float rainVolume = 0.0f;
        float windVolume = 0.0f;
        float snowVolume = 0.0f;

        // VK-1521: how long each target has been continuously silent. The debounce that
        // stops a one-frame intensity dip from arming an uncancellable 2s fade-out and
        // leaving a second stream stacked on top of it. See WeatherLoopGate.hpp.
        float rainSilentTime = 0.0f;
        float windSilentTime = 0.0f;
        float snowSilentTime = 0.0f;

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

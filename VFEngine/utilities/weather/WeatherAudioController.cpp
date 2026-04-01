#include "WeatherAudioController.hpp"
#include "events/EventDispatcher.hpp"
#include "events/audio/AudioEvents.hpp"
#include "events/audio/AudioBusEvents.hpp"
#include <algorithm>

namespace weather
{
    void WeatherAudioController::init()
    {
        try
        {
            auto& dispatcher = ::events::EventDispatcher::instance();
            events::audio::CreateBusCommand busCmd;
            busCmd.busName = "Weather";
            busCmd.parentName = "Ambient";
            dispatcher.execute(busCmd);
        }
        catch (...) {}
        initialized = true;
    }

    void WeatherAudioController::cleanup()
    {
        fadeOutLoop(rainHandle);
        fadeOutLoop(windHandle);
        fadeOutLoop(snowHandle);
        initialized = false;
    }

    void WeatherAudioController::update(float deltaTime, const WeatherState& state)
    {
        if (!initialized) return;

        if (state.precipType != activePrecipType)
        {
            if (activePrecipType == PrecipitationType::Rain) fadeOutLoop(rainHandle);
            else if (activePrecipType == PrecipitationType::Snow) fadeOutLoop(snowHandle);
            activePrecipType = state.precipType;
        }

        float windTarget = WIND_BASE_VOLUME + (state.windSpeed / 40.0f) * (WIND_MAX_VOLUME - WIND_BASE_VOLUME);
        windTarget = std::clamp(windTarget, 0.0f, WIND_MAX_VOLUME);
        updateLoop(deltaTime, windTarget, windHandle, windVolume, audioConfig.windLoopPath);

        if (windHandle != 0)
        {
            float pitch = 0.9f + (state.windSpeed / 40.0f) * 0.2f + state.gustStrength * 0.1f;
            setLoopPitch(windHandle, pitch);
        }

        float rainTarget = (state.precipType == PrecipitationType::Rain && state.precipIntensity > 0.01f)
            ? state.precipIntensity * RAIN_MAX_VOLUME : 0.0f;
        updateLoop(deltaTime, rainTarget, rainHandle, rainVolume, audioConfig.rainLoopPath);

        float snowTarget = (state.precipType == PrecipitationType::Snow && state.precipIntensity > 0.01f)
            ? state.precipIntensity * SNOW_MAX_VOLUME : 0.0f;
        updateLoop(deltaTime, snowTarget, snowHandle, snowVolume, audioConfig.snowLoopPath);
    }

    void WeatherAudioController::updateLoop(float deltaTime, float targetVolume,
                                             uint64_t& handle, float& volume, const std::string& path)
    {
        float diff = targetVolume - volume;
        volume += std::clamp(diff, -VOLUME_RAMP_SPEED * deltaTime, VOLUME_RAMP_SPEED * deltaTime);

        if (volume > 0.01f && handle == 0 && !path.empty())
            startLoop(handle, path, 0.0f);

        if (handle != 0)
            setLoopVolume(handle, volume);

        if (volume <= 0.01f && handle != 0)
            fadeOutLoop(handle);
    }

    void WeatherAudioController::startLoop(uint64_t& handle, const std::string& path, float volume)
    {
        try
        {
            auto& dispatcher = ::events::EventDispatcher::instance();
            events::audio::PlayStreamingSoundCommand cmd;
            cmd.path = path;
            cmd.params.loop = true;
            cmd.params.streaming = true;
            cmd.params.volume = volume;
            cmd.params.busName = "Weather";
            auto result = dispatcher.execute(cmd);
            handle = result.id;
        }
        catch (...) { handle = 0; }
    }

    void WeatherAudioController::fadeOutLoop(uint64_t& handle)
    {
        if (handle == 0) return;
        try
        {
            auto& dispatcher = ::events::EventDispatcher::instance();
            events::audio::FadeOutAndReleaseSoundCommand cmd;
            cmd.handle = services::AudioHandle{handle};
            cmd.fadeDurationMs = CROSSFADE_DURATION_MS;
            dispatcher.execute(cmd);
        }
        catch (...) {}
        handle = 0;
    }

    void WeatherAudioController::setLoopVolume(uint64_t handle, float volume)
    {
        if (handle == 0) return;
        try
        {
            auto& dispatcher = ::events::EventDispatcher::instance();
            events::audio::SetSoundVolumeCommand cmd;
            cmd.handle = services::AudioHandle{handle};
            cmd.volume = volume;
            dispatcher.execute(cmd);
        }
        catch (...) {}
    }

    void WeatherAudioController::setLoopPitch(uint64_t handle, float pitch)
    {
        if (handle == 0) return;
        try
        {
            auto& dispatcher = ::events::EventDispatcher::instance();
            events::audio::SetSoundPitchCommand cmd;
            cmd.handle = services::AudioHandle{handle};
            cmd.pitch = pitch;
            dispatcher.execute(cmd);
        }
        catch (...) {}
    }
}

#include "WeatherAudioController.hpp"
#include "events/EventDispatcher.hpp"
#include "events/audio/AudioEvents.hpp"
#include "events/audio/AudioBusEvents.hpp"
#include <algorithm>
#include <cmath>

namespace weather
{
    void WeatherAudioController::init()
    {
        // Create weather audio bus as child of Ambient
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
        if (!initialized)
            return;

        // Handle precipitation type changes (crossfade)
        if (state.precipType != activePrecipType)
        {
            // Fade out old precipitation loop
            if (activePrecipType == PrecipitationType::Rain)
                fadeOutLoop(rainHandle);
            else if (activePrecipType == PrecipitationType::Snow)
                fadeOutLoop(snowHandle);

            activePrecipType = state.precipType;
        }

        updateWindAmbient(deltaTime, state);
        updateRainAmbient(deltaTime, state);
        updateSnowAmbient(deltaTime, state);
    }

    void WeatherAudioController::updateWindAmbient(float deltaTime, const WeatherState& state)
    {
        // Wind is always present, volume scales with wind speed
        float targetVolume = WIND_BASE_VOLUME + (state.windSpeed / 40.0f) * (WIND_MAX_VOLUME - WIND_BASE_VOLUME);
        targetVolume = std::clamp(targetVolume, 0.0f, WIND_MAX_VOLUME);

        // Ramp volume smoothly
        float diff = targetVolume - windVolume;
        windVolume += std::clamp(diff, -VOLUME_RAMP_SPEED * deltaTime, VOLUME_RAMP_SPEED * deltaTime);

        if (windVolume > 0.01f && windHandle == 0 && !audioConfig.windLoopPath.empty())
        {
            startLoop(windHandle, audioConfig.windLoopPath, 0.0f);
        }

        if (windHandle != 0)
        {
            setLoopVolume(windHandle, windVolume);

            // Modulate pitch with wind speed and gusts
            float basePitch = 0.9f + (state.windSpeed / 40.0f) * 0.2f;
            float gustPitch = state.gustStrength * 0.1f;
            setLoopPitch(windHandle, basePitch + gustPitch);
        }
    }

    void WeatherAudioController::updateRainAmbient(float deltaTime, const WeatherState& state)
    {
        float targetVolume = 0.0f;
        if (state.precipType == PrecipitationType::Rain && state.precipIntensity > 0.01f)
            targetVolume = state.precipIntensity * RAIN_MAX_VOLUME;

        float diff = targetVolume - rainVolume;
        rainVolume += std::clamp(diff, -VOLUME_RAMP_SPEED * deltaTime, VOLUME_RAMP_SPEED * deltaTime);

        if (rainVolume > 0.01f && rainHandle == 0 && !audioConfig.rainLoopPath.empty())
        {
            startLoop(rainHandle, audioConfig.rainLoopPath, 0.0f);
        }

        if (rainHandle != 0)
        {
            setLoopVolume(rainHandle, rainVolume);
        }

        // Cleanup when volume reaches zero
        if (rainVolume <= 0.01f && rainHandle != 0)
        {
            fadeOutLoop(rainHandle);
        }
    }

    void WeatherAudioController::updateSnowAmbient(float deltaTime, const WeatherState& state)
    {
        float targetVolume = 0.0f;
        if (state.precipType == PrecipitationType::Snow && state.precipIntensity > 0.01f)
            targetVolume = state.precipIntensity * SNOW_MAX_VOLUME;

        float diff = targetVolume - snowVolume;
        snowVolume += std::clamp(diff, -VOLUME_RAMP_SPEED * deltaTime, VOLUME_RAMP_SPEED * deltaTime);

        if (snowVolume > 0.01f && snowHandle == 0 && !audioConfig.snowLoopPath.empty())
        {
            startLoop(snowHandle, audioConfig.snowLoopPath, 0.0f);
        }

        if (snowHandle != 0)
        {
            setLoopVolume(snowHandle, snowVolume);
        }

        if (snowVolume <= 0.01f && snowHandle != 0)
        {
            fadeOutLoop(snowHandle);
        }
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
        catch (...)
        {
            handle = 0;
        }
    }

    void WeatherAudioController::fadeOutLoop(uint64_t& handle)
    {
        if (handle == 0)
            return;

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
        if (handle == 0)
            return;

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
        if (handle == 0)
            return;

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

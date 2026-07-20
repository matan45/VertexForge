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
        updateLoop(deltaTime, windTarget, windHandle, windVolume, windSilentTime,
                   audioConfig.windLoopPath);

        if (windHandle != 0)
        {
            float pitch = 0.9f + (state.windSpeed / 40.0f) * 0.2f + state.gustStrength * 0.1f;
            setLoopPitch(windHandle, pitch);
        }

        float rainTarget = (state.precipType == PrecipitationType::Rain && state.precipIntensity > 0.01f)
            ? state.precipIntensity * RAIN_MAX_VOLUME : 0.0f;
        updateLoop(deltaTime, rainTarget, rainHandle, rainVolume, rainSilentTime,
                   audioConfig.rainLoopPath);

        float snowTarget = (state.precipType == PrecipitationType::Snow && state.precipIntensity > 0.01f)
            ? state.precipIntensity * SNOW_MAX_VOLUME : 0.0f;
        updateLoop(deltaTime, snowTarget, snowHandle, snowVolume, snowSilentTime,
                   audioConfig.snowLoopPath);
    }

    void WeatherAudioController::updateLoop(float deltaTime, float targetVolume,
                                             uint64_t& handle, float& volume, float& silentTime,
                                             const std::string& path)
    {
        // The decision itself is pure and lives in WeatherLoopGate.hpp; this function is
        // only the part that talks to the engine. The split keeps the stacking regression
        // independently testable without audio/event machinery.
        LoopGateState gate;
        gate.volume = volume;
        gate.silentTime = silentTime;

        const LoopGateDecision d = evaluateLoopGate(gate, targetVolume, deltaTime,
                                                    handle != 0, !path.empty(),
                                                    VOLUME_RAMP_SPEED);

        volume = gate.volume;
        silentTime = gate.silentTime;

        if (d.start)
            startLoop(handle, path, d.appliedVolume, CROSSFADE_DURATION_MS);

        // Safe during a fade-in: this lands in StreamingAudioManager::setVolume, which
        // re-bases the ramp's target instead of overwriting AL_GAIN, so the fade survives.
        if (d.applyVolume && handle != 0)
            setLoopVolume(handle, d.appliedVolume);

        if (d.stop)
            fadeOutLoop(handle);
    }

    void WeatherAudioController::startLoop(uint64_t& handle, const std::string& path, float volume,
                                            float fadeInMs)
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
            cmd.params.fadeInMs = fadeInMs;
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

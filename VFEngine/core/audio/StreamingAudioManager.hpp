#pragma once
#include "StreamingAudioSource.hpp"
#include "AudioSourceManager.hpp"
#include <unordered_map>
#include <memory>
#include <string>

namespace core::audio
{
    // Handle range for streaming sources (high bit set to distinguish from regular handles)
    constexpr AudioHandle StreamingHandleBase = 0x8000000000000000ULL;

    class StreamingAudioManager
    {
    private:
        std::unordered_map<AudioHandle, std::unique_ptr<StreamingAudioSource>> activeSources;
        uint64_t nextHandleId = 1;

        // VK-1521: the streaming twin of AudioSourceManager::FadingSource. No poolIndex —
        // streams are keyed straight out of activeSources rather than living in a pool.
        struct FadingStream
        {
            AudioHandle handle;
            // The bus-multiplied TARGET. AL_GAIN carries baseVolume scaled by the ramp;
            // setVolume is its sole authority and re-bases it so a bus flush mid-ramp cannot
            // stomp the fade. A stream is AL_SOURCE_RELATIVE and holds no filter, so AL_GAIN
            // is its ENTIRE gain chain and this one multiplier is the complete model.
            float baseVolume;
            float startGain;  // see fade::rampGain — kept apart from baseVolume on purpose
            float remainingMs;
            float totalMs;
            fade::FadeDirection direction;
        };
        std::vector<FadingStream> fadingStreams;

        std::vector<FadingStream>::iterator findFade(AudioHandle handle);
        std::vector<FadingStream>::const_iterator findFade(AudioHandle handle) const;
        // fadingStreams is a second container keyed on the same handle as activeSources, so
        // every path that drops a source must drop its ramp too or the entry is orphaned.
        void eraseFade(AudioHandle handle);
        void updateFades(float deltaTimeMs);

    public:
        explicit StreamingAudioManager() = default;
        ~StreamingAudioManager();

        StreamingAudioManager(const StreamingAudioManager&) = delete;
        StreamingAudioManager& operator=(const StreamingAudioManager&) = delete;

        // Play a streaming audio file, returns handle for control.
        //
        // VK-1521: fadeInMs is taken here rather than armed by the caller afterwards
        // because this function calls play() itself. Arming from outside would leave the
        // mixer running at full config.volume for the gap between the two calls, which is
        // the pop the fade exists to remove. Not rampable (0 / negative / non-finite)
        // means no fade, i.e. exactly the pre-VK-1521 behaviour.
        AudioHandle playStreaming(const std::string& path, const AudioSourceConfig& config,
                                  const StreamingConfig& streamConfig = {},
                                  float fadeInMs = 0.0f);

        // Control existing streaming sources
        void stop(AudioHandle handle);
        void pause(AudioHandle handle);
        void resume(AudioHandle handle);

        void setVolume(AudioHandle handle, float volume);
        void setPitch(AudioHandle handle, float pitch);
        void setLooping(AudioHandle handle, bool loop);
        // VK-1515: AL_GAIN carries userVolume * effectiveBusVolume, so this is what the
        // overlay needs to report a stream's audible level the same way a pooled voice's is.
        // VK-1521: it now also carries the fade, so the overlay picks the ramp up for free
        // and must NOT additionally scale by getFadeGain (that squares it).
        float getVolume(AudioHandle handle) const;

        // VK-1521: the twin of AudioSourceManager::getFadeGain, and needed for the same
        // reason — publishSnapshot's bus-meter term deliberately omits the source's own gain
        // (the bus applies userVolume downstream), so it passes no gain at all and has to
        // re-apply the ramp by hand. 1.0f when the handle is not fading.
        float getFadeGain(AudioHandle handle) const;

        bool isPlaying(AudioHandle handle) const;
        bool isFinished(AudioHandle handle) const;
        float getPlaybackPosition(AudioHandle handle) const;
        StreamingPlaybackMetrics getPlaybackMetrics(AudioHandle handle) const;
        bool setPlaybackPosition(AudioHandle handle, float seconds);
        float getDuration(AudioHandle handle) const;

        // Check if handle is a streaming handle
        static bool isStreamingHandle(AudioHandle handle)
        {
            return (handle & StreamingHandleBase) != 0;
        }

        // VK-1521. Mirrors AudioSourceManager's pooled fade, and for the same reasons: the
        // ramp state must live where setVolume can see it (the bus flushes a fade-free
        // userVolume * effectiveBusVolume through setVolume every tick and would otherwise
        // stomp the ramp), and where the erase happens (a fading-out stream must outlive
        // its stop request, which today destroys the source outright).
        //
        // Deliberately NOT inside StreamingAudioSource, which the ticket sketched: that
        // class has no clock, its update() early-outs on anything but Playing, and it
        // caches no volume at all (setVolume/getVolume hit AL directly, so ramping from
        // getVolume() would compound into an exponential rather than a linear fade).
        void startFadeIn(AudioHandle handle, float durationMs);
        void startFadeOut(AudioHandle handle, float durationMs);

        void stopAll();

        // Must be called from the audio thread's tick. deltaTimeMs is MILLISECONDS, matching
        // AudioSourceManager::updateFades (the rest of the tick is in seconds).
        void update(float deltaTimeMs);

        // Get the OpenAL source ID for a streaming handle
        ALuint getSourceId(AudioHandle handle) const;

        // Get count of active streaming sources
        size_t getActiveCount() const { return activeSources.size(); }

    private:
        AudioHandle generateHandle();
        void cleanupFinishedSources();
    };
}

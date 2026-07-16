#include "StreamingAudioManager.hpp"
#include "print/Log.hpp"
#include <algorithm>

namespace core::audio {

    StreamingAudioManager::~StreamingAudioManager() {
        stopAll();
        activeSources.clear();
    }

    AudioHandle StreamingAudioManager::generateHandle() {
        return StreamingHandleBase | (nextHandleId++);
    }

    AudioHandle StreamingAudioManager::playStreaming(const std::string& path,
                                                      const AudioSourceConfig& config,
                                                      const StreamingConfig& streamConfig,
                                                      float fadeInMs) {
        auto source = std::make_unique<StreamingAudioSource>();

        if (!source->open(path, streamConfig)) {
            vfLogError("Failed to open streaming audio: {}", path);
            return InvalidAudioHandle;
        }

        source->applyConfig(config);

        AudioHandle handle = generateHandle();

        // VK-1521: armed HERE, between applyConfig and play(), rather than by the caller
        // afterwards — play() below starts the mixer, so a caller arming after this returns
        // would leak an instant of full-gain audio, which is the pop the fade exists to
        // remove. baseVolume starts as the raw config volume; the caller's assignSource lands
        // in setVolume moments later and re-bases it to userVolume * effectiveBusVolume, with
        // the ramp reapplied on top.
        if (fade::isRampable(fadeInMs)) {
            fadingStreams.push_back({handle, config.volume, 1.0f, fadeInMs, fadeInMs,
                                     fade::FadeDirection::In});
            source->setVolume(config.volume * fade::rampGain(fade::FadeDirection::In, 1.0f,
                                                             fadeInMs, fadeInMs));
        }

        source->play();
        activeSources[handle] = std::move(source);

        return handle;
    }

    void StreamingAudioManager::stop(AudioHandle handle) {
        auto it = activeSources.find(handle);
        if (it != activeSources.end()) {
            it->second->stop();
            activeSources.erase(it);
        }
        // A hard stop cancels any ramp. Unconditional: the entry must go even if the source
        // was already gone, or updateFades would keep advancing a handle nothing owns.
        eraseFade(handle);
    }

    void StreamingAudioManager::pause(AudioHandle handle) {
        auto it = activeSources.find(handle);
        if (it != activeSources.end()) {
            it->second->pause();
        }
    }

    void StreamingAudioManager::resume(AudioHandle handle) {
        auto it = activeSources.find(handle);
        if (it != activeSources.end()) {
            it->second->play();
        }
    }

    void StreamingAudioManager::setVolume(AudioHandle handle, float volume) {
        auto it = activeSources.find(handle);
        if (it == activeSources.end())
            return;

        // VK-1521: the bus flushes userVolume * effectiveBusVolume through here every tick
        // (and continuously while a bus ducks), with no fade term in it — so writing it
        // straight to AL_GAIN would stomp an in-flight ramp. Re-base instead, exactly as
        // AudioSourceManager::setVolume has always done for pooled voices. Weather drives
        // this every frame while its loop fades in, so it is not a corner case.
        const auto fadeIt = findFade(handle);
        if (fadeIt != fadingStreams.end()) {
            fadeIt->baseVolume = volume;
            it->second->setVolume(volume * fade::rampGain(fadeIt->direction, fadeIt->startGain,
                                                          fadeIt->remainingMs, fadeIt->totalMs));
            return;
        }

        it->second->setVolume(volume);
    }

    float StreamingAudioManager::getFadeGain(AudioHandle handle) const {
        const auto it = findFade(handle);
        if (it == fadingStreams.end())
            return 1.0f;
        // Must be the same expression updateFades multiplies into AL_GAIN.
        return fade::rampGain(it->direction, it->startGain, it->remainingMs, it->totalMs);
    }

    float StreamingAudioManager::getVolume(AudioHandle handle) const {
        auto it = activeSources.find(handle);
        if (it != activeSources.end()) {
            return it->second->getVolume();
        }
        return 0.0f;
    }

    void StreamingAudioManager::setPitch(AudioHandle handle, float pitch) {
        auto it = activeSources.find(handle);
        if (it != activeSources.end()) {
            it->second->setPitch(pitch);
        }
    }

    void StreamingAudioManager::setLooping(AudioHandle handle, bool loop) {
        auto it = activeSources.find(handle);
        if (it != activeSources.end()) {
            it->second->setLooping(loop);
        }
    }

    bool StreamingAudioManager::isPlaying(AudioHandle handle) const {
        auto it = activeSources.find(handle);
        if (it != activeSources.end()) {
            return it->second->isPlaying();
        }
        return false;
    }

    bool StreamingAudioManager::isFinished(AudioHandle handle) const {
        // Finished sources are erased by cleanupFinishedSources, so "not found" means finished
        return activeSources.find(handle) == activeSources.end();
    }

    float StreamingAudioManager::getPlaybackPosition(AudioHandle handle) const {
        auto it = activeSources.find(handle);
        if (it != activeSources.end()) {
            return it->second->getPlaybackPosition();
        }
        return 0.0f;
    }

    StreamingPlaybackMetrics StreamingAudioManager::getPlaybackMetrics(AudioHandle handle) const {
        auto it = activeSources.find(handle);
        if (it != activeSources.end()) {
            return it->second->getPlaybackMetrics();
        }
        return {};
    }

    bool StreamingAudioManager::setPlaybackPosition(AudioHandle handle, float seconds) {
        auto it = activeSources.find(handle);
        if (it != activeSources.end()) {
            return it->second->setPlaybackPosition(seconds);
        }
        return false;
    }

    float StreamingAudioManager::getDuration(AudioHandle handle) const {
        auto it = activeSources.find(handle);
        if (it != activeSources.end()) {
            return it->second->getDuration();
        }
        return 0.0f;
    }

    void StreamingAudioManager::stopAll() {
        for (auto& [handle, source] : activeSources) {
            source->stop();
        }
        activeSources.clear();
        fadingStreams.clear();
    }

    std::vector<StreamingAudioManager::FadingStream>::iterator
    StreamingAudioManager::findFade(AudioHandle handle) {
        return std::find_if(fadingStreams.begin(), fadingStreams.end(),
            [handle](const FadingStream& fading) { return fading.handle == handle; });
    }

    std::vector<StreamingAudioManager::FadingStream>::const_iterator
    StreamingAudioManager::findFade(AudioHandle handle) const {
        return std::find_if(fadingStreams.begin(), fadingStreams.end(),
            [handle](const FadingStream& fading) { return fading.handle == handle; });
    }

    void StreamingAudioManager::eraseFade(AudioHandle handle) {
        const auto it = findFade(handle);
        if (it != fadingStreams.end())
            fadingStreams.erase(it);
    }

    void StreamingAudioManager::startFadeIn(AudioHandle handle, float durationMs) {
        auto it = activeSources.find(handle);
        if (it == activeSources.end())
            return;
        if (findFade(handle) != fadingStreams.end())
            return;                       // already ramping — never stack a second entry
        if (!fade::isRampable(durationMs))
            return;                       // no fade: leave the stream at its full gain

        const float target = it->second->getVolume();
        fadingStreams.push_back({handle, target, 1.0f, durationMs, durationMs,
                                 fade::FadeDirection::In});
        it->second->setVolume(target * fade::rampGain(fade::FadeDirection::In, 1.0f,
                                                      durationMs, durationMs));
    }

    void StreamingAudioManager::startFadeOut(AudioHandle handle, float durationMs) {
        auto it = activeSources.find(handle);
        if (it == activeSources.end())
            return;

        auto fadeIt = findFade(handle);

        // A repeat fade-out is a no-op, mirroring the pooled path — there, a fading-out voice
        // has left activeHandles so startFadeOut's guard rejects it. A fading-out STREAM has
        // no such tell: it deliberately stays in activeSources for the whole ramp. Without
        // this, a second request would restart the ramp from where it had got to and the
        // sound would linger past the release the caller already asked for.
        if (fadeIt != fadingStreams.end() && fadeIt->direction == fade::FadeDirection::Out)
            return;

        if (!fade::isRampable(durationMs)) {
            stop(handle);                 // hard cut, and it purges the ramp entry
            return;
        }

        float baseVolume;
        float startGain;
        if (fadeIt != fadingStreams.end()) {
            // Hand off from the in-flight ramp — inherit the bus target, never re-read
            // AL_GAIN (which is base*gain and would be undone by the next bus flush).
            baseVolume = fadeIt->baseVolume;
            startGain = fade::rampGain(fadeIt->direction, fadeIt->startGain,
                                       fadeIt->remainingMs, fadeIt->totalMs);
            fadingStreams.erase(fadeIt);  // replace, never stack
        } else {
            // Steady state: AL_GAIN IS the bus-multiplied target.
            baseVolume = it->second->getVolume();
            startGain = 1.0f;
        }

        fadingStreams.push_back({handle, baseVolume, startGain, durationMs, durationMs,
                                 fade::FadeDirection::Out});
    }

    void StreamingAudioManager::updateFades(float deltaTimeMs) {
        auto it = fadingStreams.begin();
        while (it != fadingStreams.end()) {
            auto sourceIt = activeSources.find(it->handle);
            if (sourceIt == activeSources.end()) {
                // The source went away underneath us — a stream that hit EOF mid-ramp and was
                // reaped by cleanupFinishedSources. Drop the orphan rather than advancing a
                // ramp nothing owns.
                it = fadingStreams.erase(it);
                continue;
            }

            it->remainingMs -= deltaTimeMs;

            if (it->remainingMs <= 0.0f) {
                if (it->direction == fade::FadeDirection::In) {
                    // A fade-in ends the RAMP, not the stream. Settle exactly on the target.
                    sourceIt->second->setVolume(
                        it->baseVolume * fade::rampGain(it->direction, it->startGain,
                                                        it->remainingMs, it->totalMs));
                    it = fadingStreams.erase(it);
                    continue;
                }

                // Fade-out complete — this is the release. Erasing destroys the source (and
                // its AL source), which flips isFinished() to true so AudioThread's snapshot
                // GC reaps the voice, exactly as the pooled path already works.
                //
                // Load-bearing for LOOPING streams (music, weather): a loop never reaches
                // StreamingState::Finished on its own, so without this an outro would fade to
                // silence and then stream forever at gain 0.
                sourceIt->second->stop();
                activeSources.erase(sourceIt);
                it = fadingStreams.erase(it);
                continue;
            }

            sourceIt->second->setVolume(
                it->baseVolume * fade::rampGain(it->direction, it->startGain,
                                                it->remainingMs, it->totalMs));
            ++it;
        }
    }

    void StreamingAudioManager::update(float deltaTimeMs) {
        // Update all active streaming sources
        for (auto& [handle, source] : activeSources) {
            source->update();
        }

        // VK-1521: before the cleanup below, so a ramp that completes on this tick releases
        // its source here and is reaped in the same pass rather than lingering a tick.
        updateFades(deltaTimeMs);

        // Clean up finished non-looping sources
        cleanupFinishedSources();
    }

    ALuint StreamingAudioManager::getSourceId(AudioHandle handle) const {
        auto it = activeSources.find(handle);
        if (it != activeSources.end()) {
            return it->second->getSourceId();
        }
        return 0;
    }

    void StreamingAudioManager::cleanupFinishedSources() {
        for (auto it = activeSources.begin(); it != activeSources.end(); ) {
            if (it->second->isFinished()) {
                // VK-1521: a stream can hit EOF mid-ramp, so drop its ramp with it —
                // fadingStreams is keyed on the same handle and would otherwise be orphaned.
                eraseFade(it->first);
                it = activeSources.erase(it);
            } else {
                ++it;
            }
        }
    }

}

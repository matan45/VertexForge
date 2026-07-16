#include "AudioSourceManager.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <cmath>

namespace core::audio {

    AudioSourceManager::AudioSourceManager(size_t initialPoolSize) {
        if (initialPoolSize > 0) {
            growPool(initialPoolSize);
        }
    }

    void AudioSourceManager::initPool(size_t poolSize) {
        if (sourcePool.empty() && poolSize > 0) {
            growPool(poolSize);
        }
    }

    AudioSourceManager::~AudioSourceManager() {
        activeHandles.clear();
        freeIndices.clear();
        sourcePool.clear();
    }

    void AudioSourceManager::growPool(size_t additionalSources) {
        sourcePool.reserve(sourcePool.size() + additionalSources);

        for (size_t i = 0; i < additionalSources; ++i) {
            auto source = std::make_unique<AudioSource>();
            if (source->isValid()) {
                sourcePool.push_back(std::move(source));
                // Drive-by (VK-1513): this used to push `startIndex + i`. If any
                // alGenSources failed mid-loop, the loop counter and the real slot index
                // desynchronized — every later index was off by the number of failures,
                // permanently leaking a slot and pushing an out-of-range index that
                // startFadeOut would then dereference unchecked. Derive it from the vector.
                freeIndices.push_back(sourcePool.size() - 1);
            } else {
                vfLogWarning("Failed to create audio source in pool");
            }
        }
    }

    AudioHandle AudioSourceManager::generateHandle() {
        return nextHandleId++;
    }

    AudioHandle AudioSourceManager::acquireSource() {
        if (freeIndices.empty()) {
            // VK-1513: growth is now bounded by the voice cap. This used to grow by 16
            // unconditionally and forever, with the only backstop being OpenAL itself
            // refusing alGenSources (~256) — at which point playback silently failed.
            // VoicePolicy decides admission before we ever get here, so with the cap on,
            // reaching this is a belt-and-braces path rather than the routine one.
            const bool capped = maxVoices > 0;
            if (!capped || sourcePool.size() < static_cast<size_t>(maxVoices)) {
                const size_t room = capped ? static_cast<size_t>(maxVoices) - sourcePool.size()
                                           : 16;
                growPool(std::min<size_t>(16, room));
            }
        }

        if (freeIndices.empty()) {
            vfLogError("Failed to acquire audio source - pool exhausted");
            return InvalidAudioHandle;
        }

        size_t index = freeIndices.back();
        freeIndices.pop_back();

        AudioHandle handle = generateHandle();
        activeHandles[handle] = index;

        return handle;
    }

    std::vector<AudioSourceManager::FadingSource>::iterator
    AudioSourceManager::findFade(AudioHandle handle) {
        return std::find_if(fadingQueue.begin(), fadingQueue.end(),
            [handle](const FadingSource& fading) { return fading.handle == handle; });
    }

    std::vector<AudioSourceManager::FadingSource>::const_iterator
    AudioSourceManager::findFade(AudioHandle handle) const {
        return std::find_if(fadingQueue.begin(), fadingQueue.end(),
            [handle](const FadingSource& fading) { return fading.handle == handle; });
    }

    void AudioSourceManager::retireSlot(size_t poolIndex) {
        if (poolIndex >= sourcePool.size())
            return;
        auto* source = sourcePool[poolIndex].get();
        source->stop();
        source->detachFilter();
        source->setBuffer(0);
        freeIndices.push_back(poolIndex);
    }

    void AudioSourceManager::releaseSource(AudioHandle handle) {
        // VK-1521: resolved BEFORE the branch, because a fading-IN handle is in both maps
        // (see the invariant in the header). The old code only cleared fadingQueue on the
        // not-in-activeHandles path, which was complete while fade-out was the only fade —
        // a fade-in reaching the branch below would have had its slot freed here and its
        // ramp left running, so updateFades would keep writing gain into a slot already
        // re-issued to another voice and would free that slot a SECOND time on completion.
        // Reachable via demoteVoice (AudioThread) releasing a voice that is still ramping.
        auto fadeIt = findFade(handle);

        auto it = activeHandles.find(handle);
        if (it == activeHandles.end()) {
            if (fadeIt == fadingQueue.end())
                return;

            retireSlot(fadeIt->poolIndex);
            fadingQueue.erase(fadeIt);
            return;
        }

        retireSlot(it->second);
        activeHandles.erase(it);
        if (fadeIt != fadingQueue.end())
            fadingQueue.erase(fadeIt);
    }

    AudioSource* AudioSourceManager::getSource(AudioHandle handle) {
        auto it = activeHandles.find(handle);
        if (it == activeHandles.end()) {
            const auto fadeIt = findFade(handle);
            if (fadeIt == fadingQueue.end() || fadeIt->poolIndex >= sourcePool.size())
                return nullptr;
            return sourcePool[fadeIt->poolIndex].get();
        }

        size_t index = it->second;
        if (index >= sourcePool.size()) {
            return nullptr;
        }

        return sourcePool[index].get();
    }

    const AudioSource* AudioSourceManager::getSource(AudioHandle handle) const {
        auto it = activeHandles.find(handle);
        if (it == activeHandles.end()) {
            const auto fadeIt = findFade(handle);
            if (fadeIt == fadingQueue.end() || fadeIt->poolIndex >= sourcePool.size())
                return nullptr;
            return sourcePool[fadeIt->poolIndex].get();
        }

        size_t index = it->second;
        if (index >= sourcePool.size()) {
            return nullptr;
        }

        return sourcePool[index].get();
    }

    void AudioSourceManager::setVolume(AudioHandle handle, float volume)
    {
        const auto fadeIt = findFade(handle);
        if (fadeIt != fadingQueue.end())
        {
            // The bus flushes userVolume * effectiveBusVolume through here every tick (and
            // continuously while a bus is ducking), with no fade term in it. Re-base rather
            // than write it straight to AL_GAIN, or the flush would stomp the ramp — the
            // failure mode is intermittent, which is exactly why it re-bases by hand.
            fadeIt->baseVolume = volume;
            if (fadeIt->poolIndex < sourcePool.size())
            {
                const float gain = fade::rampGain(fadeIt->direction, fadeIt->startGain,
                                                  fadeIt->remainingMs, fadeIt->totalMs);
                sourcePool[fadeIt->poolIndex]->setVolume(volume * gain);
            }
            return;
        }

        if (AudioSource* source = getSource(handle))
            source->setVolume(volume);
    }

    void AudioSourceManager::stopAll() {
        for (auto& [handle, index] : activeHandles) {
            if (index < sourcePool.size()) {
                sourcePool[index]->stop();
            }
        }
    }

    void AudioSourceManager::update() {
        std::vector<AudioHandle> toRelease;

        for (auto& [handle, index] : activeHandles) {
            if (index < sourcePool.size()) {
                auto* source = sourcePool[index].get();
                if (source->isStopped() && !source->isLooping()) {
                    toRelease.push_back(handle);
                }
            }
        }

        for (AudioHandle handle : toRelease) {
            releaseSource(handle);
        }
    }

    void AudioSourceManager::startFadeIn(AudioHandle handle, float durationMs)
    {
        // A fade-in only ever arms a voice that is already live and un-faded: the caller is
        // the play path, one line before source->play(). A handle that is mid-fade-out is
        // deliberately not revivable this way — it is on its way to being released.
        const auto it = activeHandles.find(handle);
        if (it == activeHandles.end())
            return;
        if (findFade(handle) != fadingQueue.end())
            return; // already ramping — never stack a second entry (see the invariant)

        // Not rampable means "no fade", i.e. leave the voice at the full gain the caller
        // already applied. This is what makes fadeInMs = 0 byte-identical to pre-VK-1521.
        if (!fade::isRampable(durationMs))
            return;

        const size_t index = it->second;
        if (index >= sourcePool.size())
            return;

        auto* source = sourcePool[index].get();
        // baseVolume is the ramp's TARGET: whatever gain the caller has just settled on
        // (assignSource writes userVolume * effectiveBusVolume immediately before this).
        // Safe to read back here precisely because no ramp is in flight yet — the guard
        // above rejected that case — so AL_GAIN IS the bus target.
        const float target = source->getVolume();

        fadingQueue.push_back({handle, index, target, 1.0f, durationMs, durationMs,
                               fade::FadeDirection::In});

        // Silence it NOW — the caller has not called play() yet, so the very first buffer
        // the mixer touches is already at the bottom of the ramp and there is no pop.
        source->setVolume(target * fade::rampGain(fade::FadeDirection::In, 1.0f,
                                                  durationMs, durationMs));

        // handle deliberately STAYS in activeHandles: a fading-in voice is an ordinary
        // playing voice, so update() must still reap it when its clip ends and
        // updateFilters must still drive its distance filter. See the header invariant.
    }

    void AudioSourceManager::startFadeOut(AudioHandle handle, float durationMs)
    {
        // Guard deliberately UNCHANGED from pre-VK-1521: the handle must be in
        // activeHandles. A voice already fading OUT is not (this function removed it), so a
        // repeat fade-out stays the no-op it has always been rather than restarting the ramp.
        // The one case newly admitted is a handle mid-fade-IN — which, by the invariant in
        // the header, is still in activeHandles precisely because it is still a normal voice.
        auto it = activeHandles.find(handle);
        if (it == activeHandles.end())
            return;

        if (!fade::isRampable(durationMs))
        {
            // Hard cut. releaseSource handles both maps, so it cleans up whichever
            // membership this handle actually had.
            releaseSource(handle);
            return;
        }

        const size_t index = it->second;
        // Drive-by (VK-1513): this dereferenced sourcePool[index] unchecked, unlike
        // updateFades below which bounds-checks the same index. Harmless once growPool's
        // index desync is fixed, but the asymmetry was an oversight.
        if (index >= sourcePool.size()) {
            activeHandles.erase(it);
            if (const auto stale = findFade(handle); stale != fadingQueue.end())
                fadingQueue.erase(stale);   // a fade-in entry would otherwise be orphaned
            return;
        }

        float baseVolume;
        float startGain;
        auto fadeIt = findFade(handle);   // only ever a fade-IN here, per the guard above
        if (fadeIt != fadingQueue.end())
        {
            // Hand off from the ramp already in flight: continue from the multiplier the
            // voice audibly reached instead of jumping to full and falling.
            //
            // baseVolume is INHERITED, never re-read from AL_GAIN. AL_GAIN is base*gain, so
            // capturing it as the new base would be silently undone by the very next bus
            // flush (which rewrites baseVolume to the real target) and the fade-out would
            // jump up mid-ramp. Intermittent, ducking-dependent, and it would ship.
            baseVolume = fadeIt->baseVolume;
            startGain = fade::rampGain(fadeIt->direction, fadeIt->startGain,
                                       fadeIt->remainingMs, fadeIt->totalMs);
            // REPLACE, never stack: the queue is scanned with find_if, so a second entry for
            // this handle would shadow the one updateFades advances — and both would free
            // the same poolIndex on completion.
            fadingQueue.erase(fadeIt);
        }
        else
        {
            // Steady state: nothing is ramping, so AL_GAIN IS the bus-multiplied target.
            baseVolume = sourcePool[index]->getVolume();
            startGain = 1.0f;
        }

        fadingQueue.push_back({handle, index, baseVolume, startGain, durationMs, durationMs,
                               fade::FadeDirection::Out});

        // Remove from activeHandles so normal update() doesn't auto-release it out from
        // under the ramp. `it` survived the fadingQueue mutation above — different container.
        activeHandles.erase(it);
    }

    void AudioSourceManager::updateFades(float deltaTimeMs)
    {
        auto it = fadingQueue.begin();
        while (it != fadingQueue.end())
        {
            it->remainingMs -= deltaTimeMs;

            if (it->remainingMs <= 0.0f)
            {
                if (it->direction == fade::FadeDirection::In)
                {
                    // VK-1521: a fade-in ends the RAMP, not the voice. Settle exactly on the
                    // target and drop the entry; the handle never left activeHandles, so
                    // update() takes over reaping it when the clip actually finishes.
                    // rampGain is exact at the endpoint, so this lands on baseVolume rather
                    // than stranding the voice at ~99% until the next bus flush.
                    if (it->poolIndex < sourcePool.size())
                        sourcePool[it->poolIndex]->setVolume(
                            it->baseVolume * fade::rampGain(it->direction, it->startGain,
                                                            it->remainingMs, it->totalMs));
                    it = fadingQueue.erase(it);
                    continue;
                }

                // Fade complete — stop and release. Deliberately does not write the final 0
                // gain: the source is being stopped and its buffer detached anyway.
                retireSlot(it->poolIndex);
                it = fadingQueue.erase(it);
            }
            else
            {
                const float gain = fade::rampGain(it->direction, it->startGain,
                                                  it->remainingMs, it->totalMs);
                if (it->poolIndex < sourcePool.size())
                    sourcePool[it->poolIndex]->setVolume(it->baseVolume * gain);
                ++it;
            }
        }
    }

    float AudioSourceManager::getFadeGain(AudioHandle handle) const
    {
        const auto it = findFade(handle);
        if (it == fadingQueue.end())
            return 1.0f;
        // Must be the SAME expression updateFades multiplies into AL_GAIN — publishSnapshot
        // re-applies this for the bus meters and would square the ramp if the two drifted.
        // Note this rises for a fade-IN: returning 1.0f there would spike the bus meters to
        // full at the start of every fade-in.
        return fade::rampGain(it->direction, it->startGain, it->remainingMs, it->totalMs);
    }

    void AudioSourceManager::updateFilters(const glm::vec3& listenerPos, float deltaTime) {
        for (auto& [handle, index] : activeHandles) {
            if (index < sourcePool.size()) {
                auto* source = sourcePool[index].get();
                if (source->isPlaying()) {
                    float distance = glm::distance(listenerPos, source->getPosition());
                    source->updateDistanceFilter(distance, deltaTime);
                }
            }
        }
    }

}

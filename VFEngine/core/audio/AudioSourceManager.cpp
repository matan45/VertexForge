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

    void AudioSourceManager::releaseSource(AudioHandle handle) {
        auto it = activeHandles.find(handle);
        if (it == activeHandles.end()) {
            auto fadeIt = std::find_if(fadingQueue.begin(), fadingQueue.end(),
                [handle](const FadingSource& fading) { return fading.handle == handle; });
            if (fadeIt == fadingQueue.end())
                return;

            if (fadeIt->poolIndex < sourcePool.size()) {
                auto* source = sourcePool[fadeIt->poolIndex].get();
                source->stop();
                source->detachFilter();
                source->setBuffer(0);
                freeIndices.push_back(fadeIt->poolIndex);
            }
            fadingQueue.erase(fadeIt);
            return;
        }

        size_t index = it->second;

        if (index < sourcePool.size()) {
            sourcePool[index]->stop();
            sourcePool[index]->detachFilter();
            sourcePool[index]->setBuffer(0);
            freeIndices.push_back(index);
        }

        activeHandles.erase(it);
    }

    AudioSource* AudioSourceManager::getSource(AudioHandle handle) {
        auto it = activeHandles.find(handle);
        if (it == activeHandles.end()) {
            const auto fadeIt = std::find_if(fadingQueue.begin(), fadingQueue.end(),
                [handle](const FadingSource& fading) { return fading.handle == handle; });
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
            const auto fadeIt = std::find_if(fadingQueue.begin(), fadingQueue.end(),
                [handle](const FadingSource& fading) { return fading.handle == handle; });
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

    void AudioSourceManager::startFadeOut(AudioHandle handle, float durationMs)
    {
        auto it = activeHandles.find(handle);
        if (it == activeHandles.end())
            return;

        size_t index = it->second;
        // Drive-by (VK-1513): this dereferenced sourcePool[index] unchecked, unlike
        // updateFades below which bounds-checks the same index. Harmless once growPool's
        // index desync is fixed, but the asymmetry was an oversight.
        if (index >= sourcePool.size()) {
            activeHandles.erase(it);
            return;
        }

        auto* source = sourcePool[index].get();
        float currentVolume = source->getVolume();

        fadingQueue.push_back({handle, index, currentVolume, durationMs, durationMs});

        // Remove from activeHandles so normal update() doesn't auto-release it
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
                // Fade complete — stop and release
                if (it->poolIndex < sourcePool.size())
                {
                    auto* source = sourcePool[it->poolIndex].get();
                    source->stop();
                    source->detachFilter();
                    source->setBuffer(0);
                    freeIndices.push_back(it->poolIndex);
                }
                it = fadingQueue.erase(it);
            }
            else
            {
                // Ramp volume down
                float t = it->remainingMs / it->totalMs;
                float fadedVolume = it->originalVolume * t;
                if (it->poolIndex < sourcePool.size())
                    sourcePool[it->poolIndex]->setVolume(fadedVolume);
                ++it;
            }
        }
    }

    float AudioSourceManager::getFadeGain(AudioHandle handle) const
    {
        const auto it = std::find_if(fadingQueue.begin(), fadingQueue.end(),
            [handle](const FadingSource& fading) { return fading.handle == handle; });
        if (it == fadingQueue.end())
            return 1.0f;
        if (!std::isfinite(it->totalMs) || it->totalMs <= 0.0f)
            return 0.0f;
        return std::clamp(it->remainingMs / it->totalMs, 0.0f, 1.0f);
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

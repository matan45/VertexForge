#include "AudioSourceManager.hpp"
#include "print/Log.hpp"
#include <algorithm>

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
        size_t startIndex = sourcePool.size();
        sourcePool.reserve(sourcePool.size() + additionalSources);

        for (size_t i = 0; i < additionalSources; ++i) {
            auto source = std::make_unique<AudioSource>();
            if (source->isValid()) {
                sourcePool.push_back(std::move(source));
                freeIndices.push_back(startIndex + i);
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
            growPool(16);
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
            return nullptr;
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
            return nullptr;
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

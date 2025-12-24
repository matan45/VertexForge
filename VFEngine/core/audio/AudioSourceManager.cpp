#include "AudioSourceManager.hpp"
#include <spdlog/spdlog.h>
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
        // Note: Don't call stopAll() here - the OpenAL context may already be destroyed.
        // The AudioController::cleanUp() should call stopAll() before destroying the AudioSystem.
        // Just clear our source pool (AudioSource destructors will handle cleanup if context is valid).
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
                spdlog::warn("Failed to create audio source in pool");
            }
        }

        spdlog::debug("Audio source pool grown to {} sources", sourcePool.size());
    }

    AudioHandle AudioSourceManager::generateHandle() {
        return nextHandleId++;
    }

    AudioHandle AudioSourceManager::acquireSource() {
        if (freeIndices.empty()) {
            growPool(16);
        }

        if (freeIndices.empty()) {
            spdlog::error("Failed to acquire audio source - pool exhausted");
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

    void AudioSourceManager::pauseAll() {
        for (auto& [handle, index] : activeHandles) {
            if (index < sourcePool.size() && sourcePool[index]->isPlaying()) {
                sourcePool[index]->pause();
            }
        }
    }

    void AudioSourceManager::resumeAll() {
        for (auto& [handle, index] : activeHandles) {
            if (index < sourcePool.size() && sourcePool[index]->isPaused()) {
                sourcePool[index]->play();
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

}

#pragma once
#include "AudioSource.hpp"
#include "VoicePolicy.hpp" // kDefaultMaxRealVoices — the pool's ceiling is the voice budget
#include <glm/glm.hpp>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <memory>

namespace core::audio
{
    using AudioHandle = uint64_t;
    constexpr AudioHandle InvalidAudioHandle = 0;

    class AudioSourceManager
    {
    private:
        std::vector<std::unique_ptr<AudioSource>> sourcePool;
        std::vector<size_t> freeIndices;
        std::unordered_map<AudioHandle, size_t> activeHandles;

        uint64_t nextHandleId = 1;

        // VK-1513: ceiling on how far the pool may grow. <= 0 means unbounded (cap off).
        // Overwritten by ApplySettingsCmd from the scene's AudioSettings::maxRealVoices;
        // this is only the pre-settings default. The pool still grows lazily rather than
        // being pre-sized, because initPool() runs during AudioController::init() while the
        // settings only arrive later — the cap is simply not known yet at init time.
        int maxVoices = kDefaultMaxRealVoices;

    public:
        explicit AudioSourceManager(size_t initialPoolSize = 32);
        ~AudioSourceManager();

        AudioSourceManager(const AudioSourceManager&) = delete;
        AudioSourceManager& operator=(const AudioSourceManager&) = delete;

        AudioHandle acquireSource();
        void releaseSource(AudioHandle handle);

        AudioSource* getSource(AudioHandle handle);
        const AudioSource* getSource(AudioHandle handle) const;

        void stopAll();
        void update();
        void updateFilters(const glm::vec3& listenerPos, float deltaTime);

        void startFadeOut(AudioHandle handle, float durationMs);
        void updateFades(float deltaTimeMs);

        size_t getActiveCount() const { return activeHandles.size(); }
        size_t getPoolSize() const { return sourcePool.size(); }

        // VK-1513. The pool is never shrunk — destroying AL sources under live voices would
        // cut them off mid-playback. Lowering the cap only stops further growth.
        void setMaxVoices(int cap) { maxVoices = cap; }
        int getMaxVoices() const { return maxVoices; }

        void initPool(size_t poolSize);

    private:
        struct FadingSource
        {
            AudioHandle handle;
            size_t poolIndex;
            float originalVolume;
            float remainingMs;
            float totalMs;
        };
        std::vector<FadingSource> fadingQueue;

        void growPool(size_t additionalSources);
        AudioHandle generateHandle();
    };
}

#pragma once
#include "AudioSource.hpp"
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <memory>

namespace core::audio {

    using AudioHandle = uint64_t;
    constexpr AudioHandle InvalidAudioHandle = 0;

    class AudioSourceManager {
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
        void pauseAll();
        void resumeAll();

        void update();

        size_t getActiveCount() const { return activeHandles.size(); }
        size_t getPoolSize() const { return sourcePool.size(); }

        void initPool(size_t poolSize);

    private:
        void growPool(size_t additionalSources);
        AudioHandle generateHandle();

        std::vector<std::unique_ptr<AudioSource>> sourcePool;
        std::vector<size_t> freeIndices;
        std::unordered_map<AudioHandle, size_t> activeHandles;

        uint64_t nextHandleId = 1;
    };

}

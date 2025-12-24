#pragma once
#include "../../services/providers/IAudioProvider.hpp"
#include <memory>

namespace core::audio {
    class AudioController;
}

namespace core {

    class AudioAdapter : public services::IAudioProvider {
    public:
        AudioAdapter();
        ~AudioAdapter() override;

        AudioAdapter(const AudioAdapter&) = delete;
        AudioAdapter& operator=(const AudioAdapter&) = delete;

        bool init() override;
        void cleanUp() override;
        void update() override;
        bool isInitialized() const override;

        void setMasterVolume(float volume) override;
        float getMasterVolume() const override;

        services::AudioHandleId playSound(const std::string& path,
                                           const services::AudioPlayParams& params) override;
        services::AudioHandleId playSound3D(const std::string& path, const glm::vec3& position,
                                             const services::AudioPlayParams& params) override;

        void stopSound(services::AudioHandleId handle) override;
        void pauseSound(services::AudioHandleId handle) override;
        void resumeSound(services::AudioHandleId handle) override;
        bool isPlaying(services::AudioHandleId handle) const override;

        void setVolume(services::AudioHandleId handle, float volume) override;
        void setPitch(services::AudioHandleId handle, float pitch) override;
        void setPosition(services::AudioHandleId handle, const glm::vec3& position) override;
        void setLooping(services::AudioHandleId handle, bool loop) override;

        void stopAll() override;
        void pauseAll() override;
        void resumeAll() override;

        void setListenerPosition(const glm::vec3& position, const glm::vec3& forward,
                                  const glm::vec3& up) override;
        void setListenerVelocity(const glm::vec3& velocity) override;

        void setDistanceModel(services::AudioDistanceModel model) override;
        void setDopplerFactor(float factor) override;
        void setSpeedOfSound(float speed) override;

        bool loadBuffer(const std::string& path) override;
        void unloadBuffer(const std::string& path) override;
        bool isBufferLoaded(const std::string& path) const override;

        size_t getActiveSourceCount() const override;
        size_t getLoadedBufferCount() const override;

    private:
        std::unique_ptr<audio::AudioController> audioController;
    };

}

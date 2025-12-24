#pragma once
#include "../interfaces/IAudioService.hpp"
#include "../providers/IAudioProvider.hpp"
#include <unordered_map>

namespace services {

    class AudioServiceImpl : public IAudioService {
    public:
        explicit AudioServiceImpl(IAudioProvider* audioProvider);
        ~AudioServiceImpl() override;

        void registerEventHandlers();

        // === Global Audio Control ===
        void setMasterVolume(float volume) override;
        [[nodiscard]] float getMasterVolume() const override;
        void pauseAll() override;
        void resumeAll() override;
        void stopAll() override;

        // === Listener (Camera/Player) ===
        void setListenerPosition(const glm::vec3& position,
                                 const glm::vec3& forward,
                                 const glm::vec3& up = glm::vec3(0, 1, 0)) override;
        void setListenerVelocity(const glm::vec3& velocity) override;

        // === Sound Playback ===
        [[nodiscard]] AudioHandle playSound(const std::string& path, const AudioParams& params = {}) override;
        [[nodiscard]] AudioHandle playSound3D(const std::string& path, const glm::vec3& position,
                                               const AudioParams& params = {}) override;

        // === Sound Control ===
        void stopSound(AudioHandle handle) override;
        void pauseSound(AudioHandle handle) override;
        void resumeSound(AudioHandle handle) override;
        [[nodiscard]] bool isPlaying(AudioHandle handle) const override;
        void setVolume(AudioHandle handle, float volume) override;
        void setPitch(AudioHandle handle, float pitch) override;
        void setPosition(AudioHandle handle, const glm::vec3& position) override;

        // === Audio Source Component ===
        void addAudioSource(EntityHandle entity, const AudioSourceData& data) override;
        void removeAudioSource(EntityHandle entity) override;
        [[nodiscard]] bool hasAudioSource(EntityHandle entity) const override;
        void playEntityAudio(EntityHandle entity) override;
        void pauseEntityAudio(EntityHandle entity) override;
        void stopEntityAudio(EntityHandle entity) override;

    private:
        AudioPlayParams convertParams(const AudioParams& params) const;

        IAudioProvider* audioProvider;

        struct EntityAudioState {
            AudioSourceData data;
            AudioHandle currentHandle;
        };
        std::unordered_map<EntityHandle, EntityAudioState, EntityHandle::Hash> entityAudioSources;
    };

}

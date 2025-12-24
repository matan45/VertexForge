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

        // === 2D Audio Source Component (streaming) ===
        void addAudioSource2D(EntityHandle entity, const AudioSource2DData& data) override;
        void removeAudioSource2D(EntityHandle entity) override;
        [[nodiscard]] bool hasAudioSource2D(EntityHandle entity) const override;
        void playEntityAudio2D(EntityHandle entity) override;
        void pauseEntityAudio2D(EntityHandle entity) override;
        void stopEntityAudio2D(EntityHandle entity) override;

        // === 3D Audio Source Component (cached/spatial) ===
        void addAudioSource3D(EntityHandle entity, const AudioSource3DData& data) override;
        void removeAudioSource3D(EntityHandle entity) override;
        [[nodiscard]] bool hasAudioSource3D(EntityHandle entity) const override;
        void playEntityAudio3D(EntityHandle entity, const glm::vec3& position) override;
        void pauseEntityAudio3D(EntityHandle entity) override;
        void stopEntityAudio3D(EntityHandle entity) override;
        void updateEntityAudio3DPosition(EntityHandle entity, const glm::vec3& position) override;

        // === Streaming Audio ===
        [[nodiscard]] AudioHandle playStreamingSound(const std::string& path,
                                                      const AudioParams& params = {}) override;
        [[nodiscard]] AudioHandle playStreamingSound3D(const std::string& path, const glm::vec3& position,
                                                        const AudioParams& params = {}) override;
        [[nodiscard]] float getPlaybackPosition(AudioHandle handle) const override;
        bool setPlaybackPosition(AudioHandle handle, float seconds) override;
        [[nodiscard]] float getDuration(AudioHandle handle) const override;
        [[nodiscard]] bool isStreamingHandle(AudioHandle handle) const override;

    private:
        AudioPlayParams convertParams(const AudioParams& params) const;

        IAudioProvider* audioProvider;

        // 2D Audio source state (streaming)
        struct EntityAudio2DState {
            AudioSource2DData data;
            AudioHandle currentHandle;
        };
        std::unordered_map<EntityHandle, EntityAudio2DState, EntityHandle::Hash> entityAudio2DSources;

        // 3D Audio source state (cached/spatial)
        struct EntityAudio3DState {
            AudioSource3DData data;
            AudioHandle currentHandle;
            glm::vec3 position{0.0f};
        };
        std::unordered_map<EntityHandle, EntityAudio3DState, EntityHandle::Hash> entityAudio3DSources;
    };

}

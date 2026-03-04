#pragma once
#include "../../interfaces/audio/IAudioService.hpp"
#include "../../providers/audio/IAudioProvider.hpp"

namespace services {

    class AudioServiceImpl : public IAudioService {
    public:
        explicit AudioServiceImpl(IAudioProvider* audioProvider);
        ~AudioServiceImpl() override;

        void registerEventHandlers() override;

        // === Listener (Camera/Player) ===
        void setListenerPosition(const glm::vec3& position,
                                 const glm::vec3& forward,
                                 const glm::vec3& up = glm::vec3(0, 1, 0)) override;

        // === Sound Playback ===
        [[nodiscard]] AudioHandle playSound3D(const std::string& path, const glm::vec3& position,
                                               const AudioParams& params = {}) override;
        [[nodiscard]] AudioHandle playStreamingSound(const std::string& path,
                                                      const AudioParams& params = {}) override;

        // === Sound Control ===
        void stopSound(AudioHandle handle) override;
        void pauseSound(AudioHandle handle) override;
        void resumeSound(AudioHandle handle) override;
        [[nodiscard]] bool isPlaying(AudioHandle handle) const override;
        void setVolume(AudioHandle handle, float volume) override;
        void setPitch(AudioHandle handle, float pitch) override;

        // === Playback Position ===
        [[nodiscard]] float getPlaybackPosition(AudioHandle handle) const override;
        bool setPlaybackPosition(AudioHandle handle, float seconds) override;
        [[nodiscard]] float getDuration(AudioHandle handle) const override;

    private:
        AudioPlayParams convertParams(const AudioParams& params) const;

        IAudioProvider* audioProvider;
    };

}

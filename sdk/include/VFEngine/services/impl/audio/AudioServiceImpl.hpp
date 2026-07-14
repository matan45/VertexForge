#pragma once
#include "../../interfaces/audio/IAudioService.hpp"
#include "../../providers/audio/IAudioProvider.hpp"
#include "../../events/audio/AudioEvents.hpp"

namespace services {

    class AudioServiceImpl : public IAudioService {
    public:
        explicit AudioServiceImpl(IAudioProvider* audioProvider);
        ~AudioServiceImpl() override;

        void registerEventHandlers() override;

        // === Listener (Camera/Player) ===
        void setListenerPosition(const glm::vec3& position,
                                 const glm::vec3& forward,
                                 const glm::vec3& up = glm::vec3(0, 1, 0),
                                 const glm::vec3& velocity = glm::vec3(0.0f)) override;

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

        // === Audio Buses ===
        void createBus(const std::string& busName, const std::string& parentName = "Master") override;
        void setBusVolume(const std::string& busName, float volume) override;
        void setBusMuted(const std::string& busName, bool muted) override;
        void setBusSoloed(const std::string& busName, bool soloed) override;
        [[nodiscard]] float getBusVolume(const std::string& busName) const override;
        [[nodiscard]] bool isBusMuted(const std::string& busName) const override;
        [[nodiscard]] bool isBusSoloed(const std::string& busName) const override;
        [[nodiscard]] std::vector<std::string> getBusNames() const override;
        void saveMixSnapshot(const std::string& name) override;
        void loadMixSnapshot(const std::string& name) override;
        void deleteMixSnapshot(const std::string& name) override;
        [[nodiscard]] std::vector<std::string> getSnapshotNames() const override;

        // === Audio Effects ===
        bool addBusEffect(const std::string& busName, const types::BusEffectConfig& config) override;
        bool removeBusEffect(const std::string& busName, uint32_t effectId) override;
        bool updateBusEffect(const std::string& busName, uint32_t effectId, const types::BusEffectConfig& config) override;
        bool setBusEffectEnabled(const std::string& busName, uint32_t effectId, bool enabled) override;
        bool setBusEffectWetDry(const std::string& busName, uint32_t effectId, float wetDry) override;
        [[nodiscard]] std::vector<types::BusEffectConfig> getBusEffectChain(const std::string& busName) const override;
        [[nodiscard]] int getMaxEffectsPerBus() const override;

    private:
        AudioPlayParams convertParams(const AudioParams& params) const;

        IAudioProvider* audioProvider;

        // VK-1511: main-thread cache of the last listener pose. Written by the
        // SetListenerPositionCommand handler, read by GetListenerStateQuery — both
        // run synchronously on the main dispatch thread (editor: ViewPort is the sole
        // writer), so no atomics are needed and the audio-thread snapshot is bypassed.
        events::audio::ListenerState cachedListener;
    };

}

#pragma once
#include "../../services/providers/audio/IAudioProvider.hpp"
#include "../../services/events/EventTypes.hpp"
#include <memory>

namespace core::audio
{
    class AudioController;
}

namespace core
{
    class AudioAdapter : public services::IAudioProvider
    {
    private:
        std::unique_ptr<audio::AudioController> audioController;
        events::SubscriptionToken assetReleaseToken;
    public:
        explicit AudioAdapter();
        ~AudioAdapter() override;

        AudioAdapter(const AudioAdapter&) = delete;
        AudioAdapter& operator=(const AudioAdapter&) = delete;

        bool init() override;
        void cleanUp() override;
        void update() override;
        bool isInitialized() const override;

        // === Sound Playback ===
        services::AudioHandleId playSound3D(const std::string& path, const glm::vec3& position,
                                            const services::AudioPlayParams& params) override;
        services::AudioHandleId playStreamingSound(const std::string& path,
                                                   const services::AudioPlayParams& params) override;

        // === Sound Control ===
        void stopSound(services::AudioHandleId handle) override;
        void fadeOutAndRelease(services::AudioHandleId handle, float fadeDurationMs) override;
        void pauseSound(services::AudioHandleId handle) override;
        void resumeSound(services::AudioHandleId handle) override;
        bool isPlaying(services::AudioHandleId handle) const override;
        void setVolume(services::AudioHandleId handle, float volume) override;
        void setPitch(services::AudioHandleId handle, float pitch) override;

        // === Listener ===
        void setListenerPosition(const glm::vec3& position, const glm::vec3& forward,
                                 const glm::vec3& up) override;

        // === Playback Position ===
        float getPlaybackPosition(services::AudioHandleId handle) const override;
        bool setPlaybackPosition(services::AudioHandleId handle, float seconds) override;
        float getDuration(services::AudioHandleId handle) const override;

        // === Audio Settings ===
        void applySettings(const types::AudioSettings& settings) override;
        types::AudioSettings getCurrentSettings() const override;

        // === Audio Buses ===
        void createBus(const std::string& busName, const std::string& parentName = "Master") override;
        void setBusVolume(const std::string& busName, float volume) override;
        void setBusMuted(const std::string& busName, bool muted) override;
        void setBusSoloed(const std::string& busName, bool soloed) override;
        float getBusVolume(const std::string& busName) const override;
        bool isBusMuted(const std::string& busName) const override;
        std::vector<std::string> getBusNames() const override;
        void saveMixSnapshot(const std::string& name) override;
        void loadMixSnapshot(const std::string& name) override;
        void deleteMixSnapshot(const std::string& name) override;
        std::vector<std::string> getSnapshotNames() const override;

        // === Audio Effects ===
        bool addBusEffect(const std::string& busName, const types::BusEffectConfig& config) override;
        bool removeBusEffect(const std::string& busName, uint32_t effectId) override;
        bool updateBusEffect(const std::string& busName, uint32_t effectId, const types::BusEffectConfig& config) override;
        bool setBusEffectEnabled(const std::string& busName, uint32_t effectId, bool enabled) override;
        bool setBusEffectWetDry(const std::string& busName, uint32_t effectId, float wetDry) override;
        std::vector<types::BusEffectConfig> getBusEffectChain(const std::string& busName) const override;
        int getMaxEffectsPerBus() const override;

        // === Reverb Zones ===
        void* getReverbZoneManager() override;
    };
}

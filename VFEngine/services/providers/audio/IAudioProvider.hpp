#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include "types/AudioTypes.hpp"
#include "types/AudioEffectTypes.hpp"

namespace services {

    using AudioHandleId = uint64_t;
    constexpr AudioHandleId InvalidAudioHandleId = 0;

    struct AudioPlayParams {
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;
        bool is3D = false;
        glm::vec3 position{0.0f};
        float minDistance = 1.0f;
        float maxDistance = 100.0f;
        float rolloffFactor = 1.0f;
        bool streaming = false;

        bool enableDistanceFilter = true;
        float filterStartDistance = 10.0f;
        float filterMaxDistance = 100.0f;
        float filterIntensity = 1.0f;

        float innerConeAngle = 360.0f;
        float outerConeAngle = 360.0f;
        float outerConeGain = 0.0f;
        glm::vec3 direction{0.0f, 0.0f, -1.0f};

        std::string busName = "Master";

        // VK-1513: see services::AudioParams::priority. Lower = more important.
        uint8_t priority = 128;

        // VK-1521: see services::AudioParams::fadeInMs. 0 = no fade.
        float fadeInMs = 0.0f;
    };

    class IAudioProvider {
    public:
        virtual ~IAudioProvider() = default;

        virtual bool init() = 0;
        virtual void cleanUp() = 0;
        virtual void update() = 0;
        virtual bool isInitialized() const = 0;

        // === Sound Playback ===
        virtual AudioHandleId playSound3D(const std::string& path, const glm::vec3& position,
                                           const AudioPlayParams& params) = 0;
        virtual AudioHandleId playStreamingSound(const std::string& path,
                                                  const AudioPlayParams& params) = 0;

        // === Sound Control ===
        virtual void stopSound(AudioHandleId handle) = 0;
        virtual void fadeOutAndRelease(AudioHandleId handle, float fadeDurationMs) = 0;
        virtual void pauseSound(AudioHandleId handle) = 0;
        virtual void resumeSound(AudioHandleId handle) = 0;
        virtual bool isPlaying(AudioHandleId handle) const = 0;
        virtual void setVolume(AudioHandleId handle, float volume) = 0;
        virtual void setPitch(AudioHandleId handle, float pitch) = 0;
        virtual void setSourceTransform(AudioHandleId handle, const glm::vec3& position,
                                        const glm::vec3& direction, const glm::vec3& velocity) = 0;
        // VK-1518: geometry occlusion. occlusion is 0 (clear) .. 1 (blocked); lpfAmount and
        // volumeAmount are the authored CUT amounts at full occlusion (0 = inert).
        virtual void setSourceOcclusion(AudioHandleId handle, float occlusion,
                                        float lpfAmount, float volumeAmount) = 0;

        // === Listener ===
        virtual void setListenerPosition(const glm::vec3& position, const glm::vec3& forward,
                                          const glm::vec3& up, const glm::vec3& velocity) = 0;

        // === Playback Position ===
        virtual float getPlaybackPosition(AudioHandleId handle) const = 0;
        virtual bool setPlaybackPosition(AudioHandleId handle, float seconds) = 0;
        virtual float getDuration(AudioHandleId handle) const = 0;

        // === Audio Settings ===
        virtual void applySettings(const types::AudioSettings& settings) = 0;
        virtual types::AudioSettings getCurrentSettings() const = 0;
        virtual types::AudioHrtfStatus getHrtfStatus() const = 0;
        // VK-1513: live real-voice count vs the configured budget.
        virtual types::AudioVoiceStats getVoiceStats() const = 0;
        // VK-1515: one row per live voice for the editor's active-sounds overlay, and the
        // gate that makes the audio thread produce them at all. Empty while the gate is off.
        virtual std::vector<types::AudioVoiceRow> getActiveVoices() const = 0;
        virtual void setVoiceDebugEnabled(bool enabled) = 0;

        // === Audio Buses ===
        virtual void createBus(const std::string& busName, const std::string& parentName = "Master") = 0;
        virtual void setBusVolume(const std::string& busName, float volume) = 0;
        virtual void setBusMuted(const std::string& busName, bool muted) = 0;
        virtual void setBusSoloed(const std::string& busName, bool soloed) = 0;
        virtual float getBusVolume(const std::string& busName) const = 0;
        virtual bool isBusMuted(const std::string& busName) const = 0;
        virtual bool isBusSoloed(const std::string& busName) const = 0;
        virtual std::vector<std::string> getBusNames() const = 0;
        virtual std::vector<types::AudioBusLevel> getBusLevels() const = 0;
        virtual void setBusDuck(const std::string& targetBus,
                                const types::BusDuckConfig& config) = 0;
        virtual void removeBusDuck(const std::string& targetBus) = 0;
        virtual std::optional<types::BusDuckConfig> getBusDuck(
            const std::string& targetBus) const = 0;
        virtual void saveMixSnapshot(const std::string& name) = 0;
        virtual void loadMixSnapshot(const std::string& name) = 0;
        virtual void deleteMixSnapshot(const std::string& name) = 0;
        virtual std::vector<std::string> getSnapshotNames() const = 0;

        // === Audio Effects ===
        virtual bool addBusEffect(const std::string& busName, const types::BusEffectConfig& config) = 0;
        virtual bool removeBusEffect(const std::string& busName, uint32_t effectId) = 0;
        virtual bool updateBusEffect(const std::string& busName, uint32_t effectId, const types::BusEffectConfig& config) = 0;
        virtual bool setBusEffectEnabled(const std::string& busName, uint32_t effectId, bool enabled) = 0;
        virtual bool setBusEffectWetDry(const std::string& busName, uint32_t effectId, float wetDry) = 0;
        virtual std::vector<types::BusEffectConfig> getBusEffectChain(const std::string& busName) const = 0;
        virtual int getMaxEffectsPerBus() const = 0;

        // === Reverb Zones ===
        virtual void* getReverbZoneManager() = 0; // Returns core::audio::ReverbZoneManager*
    };

}

#pragma once
#include "../../data/EntityHandle.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <optional>
#include "types/AudioEffectTypes.hpp"
#include "types/AudioTypes.hpp"

namespace services {

    struct AudioHandle {
        uint64_t id = 0;
        bool isValid() const { return id != 0; }
    };

    struct AudioParams {
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;
        bool is3D = false;
        glm::vec3 position{ 0.0f };
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

        // VK-1513: voice-budget arbitration weight. LOWER = MORE IMPORTANT (0 = critical,
        // 255 = least); 128 is exactly neutral, so an un-authored sound ranks purely on
        // audibility. Appended at the tail on purpose: this struct crosses the plugin ABI
        // by reference (PluginContext::playSound3D), so the field must not slot into an
        // existing padding hole where sizeof would stay unchanged. Plugin API v19.
        uint8_t priority = 128;

        // VK-1521: ramp up from silence over this many milliseconds instead of starting at
        // full gain. 0 = no fade, so every existing caller is unchanged. Pair it with
        // fadeOutAndRelease on the outgoing sound to crossfade. Appended at the tail for the
        // same ABI reason as priority above — this struct crosses the plugin boundary by
        // reference (PluginContext::playSound3D/playStreamingSound). Plugin API v20.
        float fadeInMs = 0.0f;
    };

    class IAudioService {
    public:
        virtual ~IAudioService() = default;

        virtual void registerEventHandlers() = 0;

        // === Listener (Camera/Player) ===

        virtual void setListenerPosition(const glm::vec3& position,
                                         const glm::vec3& forward,
                                         const glm::vec3& up = glm::vec3(0, 1, 0),
                                         const glm::vec3& velocity = glm::vec3(0.0f)) = 0;

        // === Sound Playback ===

        virtual AudioHandle playSound3D(const std::string& path, const glm::vec3& position,
                                        const AudioParams& params = {}) = 0;

        virtual AudioHandle playStreamingSound(const std::string& path, const AudioParams& params = {}) = 0;

        // === Sound Control ===

        virtual void stopSound(AudioHandle handle) = 0;

        virtual void pauseSound(AudioHandle handle) = 0;

        virtual void resumeSound(AudioHandle handle) = 0;

        virtual bool isPlaying(AudioHandle handle) const = 0;

        virtual void setVolume(AudioHandle handle, float volume) = 0;

        virtual void setPitch(AudioHandle handle, float pitch) = 0;

        // === Playback Position ===

        virtual float getPlaybackPosition(AudioHandle handle) const = 0;

        virtual bool setPlaybackPosition(AudioHandle handle, float seconds) = 0;

        virtual float getDuration(AudioHandle handle) const = 0;

        // === Audio Buses ===
        virtual void createBus(const std::string& busName, const std::string& parentName = "Master") = 0;
        virtual void setBusVolume(const std::string& busName, float volume) = 0;
        virtual void setBusMuted(const std::string& busName, bool muted) = 0;
        virtual void setBusSoloed(const std::string& busName, bool soloed) = 0;
        virtual float getBusVolume(const std::string& busName) const = 0;
        virtual bool isBusMuted(const std::string& busName) const = 0;
        virtual bool isBusSoloed(const std::string& busName) const = 0;
        virtual std::vector<std::string> getBusNames() const = 0;
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
    };

}

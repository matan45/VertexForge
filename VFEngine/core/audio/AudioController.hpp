#pragma once
#include "AudioExport.hpp"
#include "AudioSystem.hpp"
#include "AudioBufferManager.hpp"
#include "AudioSourceManager.hpp"
#include "AudioListener.hpp"
#include "StreamingAudioManager.hpp"
#include "AudioBusManager.hpp"
#include "AudioEffectManager.hpp"
#include "ReverbZoneManager.hpp"
#include "AudioPlayParams.hpp"
#include "AudioCommandQueue.hpp"
#include "AudioThread.hpp"
#include "types/AudioTypes.hpp"
#include "types/AudioEffectTypes.hpp"
#include <glm/glm.hpp>
#include <string>
#include <memory>

namespace core::audio {

    #pragma warning(push)
    #pragma warning(disable: 4251)
    class VF_AUDIO_API AudioController {
    private:
        std::unique_ptr<AudioSystem> audioSystem;
        std::unique_ptr<AudioBufferManager> bufferManager;
        std::unique_ptr<AudioSourceManager> sourceManager;
        std::unique_ptr<AudioListener> listener;
        std::unique_ptr<StreamingAudioManager> streamingManager;
        std::unique_ptr<AudioBusManager> busManager;
        std::unique_ptr<AudioEffectManager> effectManager;
        std::unique_ptr<ReverbZoneManager> reverbZoneManager;

        std::unique_ptr<AudioCommandQueue> commandQueue;
        std::unique_ptr<AudioThread> audioThread;

        // Pre-generate handles on the main thread for fire-and-forget play commands
        std::atomic<uint64_t> nextMainThreadHandle{1};
        AudioHandle generateHandle(bool streaming);

        bool initialized = false;
    public:
        explicit AudioController();
        ~AudioController();

        AudioController(const AudioController&) = delete;
        AudioController& operator=(const AudioController&) = delete;

        bool init();
        void cleanUp();
        void update();

        bool isInitialized() const { return initialized; }

        // === Sound Playback ===
        AudioHandle playSound3D(const std::string& path, const glm::vec3& position,
                                 const PlaySoundParams& params = {});
        AudioHandle playStreamingSound(const std::string& path, const PlaySoundParams& params = {});

        // === Sound Control ===
        void stopSound(AudioHandle handle);
        void fadeOutAndRelease(AudioHandle handle, float fadeDurationMs);
        void pauseSound(AudioHandle handle);
        void resumeSound(AudioHandle handle);
        bool isPlaying(AudioHandle handle) const;
        void setVolume(AudioHandle handle, float volume);
        void setPitch(AudioHandle handle, float pitch);
        void setSourceTransform(AudioHandle handle, const glm::vec3& position,
                                const glm::vec3& direction, const glm::vec3& velocity);

        // === Listener ===
        void setListenerPosition(const glm::vec3& position, const glm::vec3& forward,
                                  const glm::vec3& up, const glm::vec3& velocity);

        // === Playback Position ===
        float getPlaybackPosition(AudioHandle handle) const;
        bool setPlaybackPosition(AudioHandle handle, float seconds);
        float getDuration(AudioHandle handle) const;

        // === Audio Settings ===
        void applySettings(const types::AudioSettings& settings);
        types::AudioSettings getCurrentSettings() const;

        // === Audio Buses ===
        void createBus(const std::string& busName, const std::string& parentName = "Master");
        void setBusVolume(const std::string& busName, float volume);
        void setBusMuted(const std::string& busName, bool muted);
        void setBusSoloed(const std::string& busName, bool soloed);
        float getBusVolume(const std::string& busName) const;
        bool isBusMuted(const std::string& busName) const;
        std::vector<std::string> getBusNames() const;
        void saveMixSnapshot(const std::string& name);
        void loadMixSnapshot(const std::string& name);
        void deleteMixSnapshot(const std::string& name);
        std::vector<std::string> getSnapshotNames() const;

        // === Audio Effects ===
        bool addBusEffect(const std::string& busName, const types::BusEffectConfig& config);
        bool removeBusEffect(const std::string& busName, uint32_t effectId);
        bool updateBusEffect(const std::string& busName, uint32_t effectId, const types::BusEffectConfig& config);
        bool setBusEffectEnabled(const std::string& busName, uint32_t effectId, bool enabled);
        bool setBusEffectWetDry(const std::string& busName, uint32_t effectId, float wetDry);
        std::vector<types::BusEffectConfig> getBusEffectChain(const std::string& busName) const;
        int getMaxEffectsPerBus() const;

        // === Reverb Zones ===
        ReverbZoneManager* getReverbZoneManager() { return reverbZoneManager.get(); }

        // === Buffer Management ===
        void unloadAudioBuffer(const std::string& path);

    private:
        AudioHandle playSound(const std::string& path, const PlaySoundParams& params);
        void stopAll();
    };
    #pragma warning(pop)

}

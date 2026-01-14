#pragma once
#include "AudioSystem.hpp"
#include "AudioBufferManager.hpp"
#include "AudioSourceManager.hpp"
#include "AudioListener.hpp"
#include "StreamingAudioManager.hpp"
#include "types/AudioTypes.hpp"
#include <glm/glm.hpp>
#include <string>
#include <memory>

namespace core::audio {

    struct PlaySoundParams {
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;
        bool is3D = false;
        glm::vec3 position{0.0f};
        float minDistance = 1.0f;
        float maxDistance = 100.0f;
        float rolloffFactor = 1.0f;
        bool streaming = false;
    };

    class AudioController {
    private:
        std::unique_ptr<AudioSystem> audioSystem;
        std::unique_ptr<AudioBufferManager> bufferManager;
        std::unique_ptr<AudioSourceManager> sourceManager;
        std::unique_ptr<AudioListener> listener;
        std::unique_ptr<StreamingAudioManager> streamingManager;

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
        void pauseSound(AudioHandle handle);
        void resumeSound(AudioHandle handle);
        bool isPlaying(AudioHandle handle) const;
        void setVolume(AudioHandle handle, float volume);
        void setPitch(AudioHandle handle, float pitch);

        // === Listener ===
        void setListenerPosition(const glm::vec3& position, const glm::vec3& forward,
                                  const glm::vec3& up);

        // === Playback Position ===
        float getPlaybackPosition(AudioHandle handle) const;
        bool setPlaybackPosition(AudioHandle handle, float seconds);
        float getDuration(AudioHandle handle) const;

        // === Audio Settings ===
        void applySettings(const types::AudioSettings& settings);
        types::AudioSettings getCurrentSettings() const;

    private:
        AudioHandle playSound(const std::string& path, const PlaySoundParams& params);
        void stopAll();
    };

}

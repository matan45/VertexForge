#pragma once
#include "AudioSystem.hpp"
#include "AudioBufferManager.hpp"
#include "AudioSourceManager.hpp"
#include "AudioListener.hpp"
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
        glm::vec3 velocity{0.0f};
        float minDistance = 1.0f;
        float maxDistance = 100.0f;
        float rolloffFactor = 1.0f;
    };

    class AudioController {
    public:
        AudioController();
        ~AudioController();

        AudioController(const AudioController&) = delete;
        AudioController& operator=(const AudioController&) = delete;

        bool init();
        void cleanUp();
        void update();

        bool isInitialized() const { return initialized; }

        void setMasterVolume(float volume);
        float getMasterVolume() const { return masterVolume; }

        AudioHandle playSound(const std::string& path, const PlaySoundParams& params = {});
        AudioHandle playSound3D(const std::string& path, const glm::vec3& position,
                                 const PlaySoundParams& params = {});

        void stopSound(AudioHandle handle);
        void pauseSound(AudioHandle handle);
        void resumeSound(AudioHandle handle);
        bool isPlaying(AudioHandle handle) const;

        void setVolume(AudioHandle handle, float volume);
        void setPitch(AudioHandle handle, float pitch);
        void setPosition(AudioHandle handle, const glm::vec3& position);
        void setLooping(AudioHandle handle, bool loop);

        void stopAll();
        void pauseAll();
        void resumeAll();

        void setListenerPosition(const glm::vec3& position, const glm::vec3& forward,
                                  const glm::vec3& up);
        void setListenerVelocity(const glm::vec3& velocity);

        void setDistanceModel(DistanceModel model);
        void setDopplerFactor(float factor);
        void setSpeedOfSound(float speed);

        bool loadBuffer(const std::string& path);
        void unloadBuffer(const std::string& path);
        bool isBufferLoaded(const std::string& path) const;

        size_t getActiveSourceCount() const;
        size_t getLoadedBufferCount() const;

    private:
        std::unique_ptr<AudioSystem> audioSystem;
        std::unique_ptr<AudioBufferManager> bufferManager;
        std::unique_ptr<AudioSourceManager> sourceManager;
        std::unique_ptr<AudioListener> listener;

        float masterVolume = 1.0f;
        bool initialized = false;
    };

}

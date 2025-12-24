#pragma once
#include "../data/EntityHandle.hpp"
#include <glm/glm.hpp>
#include <string>

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
        float minDistance = 1.0f;    // Distance at which sound is at full volume
        float maxDistance = 100.0f;  // Distance at which sound is inaudible
        float rolloffFactor = 1.0f;  // How quickly sound attenuates
        bool streaming = false;      // Use streaming playback for long audio
    };
    
    // 2D Audio - streaming, good for background music
    struct AudioSource2DData {
        std::string audioFilePath;
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;
    };

    // 3D Audio - cached, good for spatial sound effects
    struct AudioSource3DData {
        std::string audioFilePath;
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;
        float minDistance = 1.0f;
        float maxDistance = 100.0f;
    };
    
    class IAudioService {
    public:
        virtual ~IAudioService() = default;

        // === Global Audio Control ===
        
        virtual void setMasterVolume(float volume) = 0;

        virtual float getMasterVolume() const = 0;
        
        virtual void pauseAll() = 0;
        
        virtual void resumeAll() = 0;
        
        virtual void stopAll() = 0;

        // === Listener (Camera/Player) ===
        
        virtual void setListenerPosition(const glm::vec3& position,
                                         const glm::vec3& forward,
                                         const glm::vec3& up = glm::vec3(0, 1, 0)) = 0;
        
        virtual void setListenerVelocity(const glm::vec3& velocity) = 0;

        // === Sound Playback (Fire and Forget) ===
        
        virtual AudioHandle playSound(const std::string& path, const AudioParams& params = {}) = 0;
        
        virtual AudioHandle playSound3D(const std::string& path, const glm::vec3& position,
                                        const AudioParams& params = {}) = 0;

        // === Sound Control ===
        
        virtual void stopSound(AudioHandle handle) = 0;
        
        virtual void pauseSound(AudioHandle handle) = 0;
        
        virtual void resumeSound(AudioHandle handle) = 0;
        
        virtual bool isPlaying(AudioHandle handle) const = 0;
        
        virtual void setVolume(AudioHandle handle, float volume) = 0;
        
        virtual void setPitch(AudioHandle handle, float pitch) = 0;
        
        virtual void setPosition(AudioHandle handle, const glm::vec3& position) = 0;

        // === 2D Audio Source Component ===

        virtual void addAudioSource2D(EntityHandle entity, const AudioSource2DData& data) = 0;
        virtual void removeAudioSource2D(EntityHandle entity) = 0;
        virtual bool hasAudioSource2D(EntityHandle entity) const = 0;
        virtual void playEntityAudio2D(EntityHandle entity) = 0;
        virtual void pauseEntityAudio2D(EntityHandle entity) = 0;
        virtual void stopEntityAudio2D(EntityHandle entity) = 0;

        // === 3D Audio Source Component ===

        virtual void addAudioSource3D(EntityHandle entity, const AudioSource3DData& data) = 0;
        virtual void removeAudioSource3D(EntityHandle entity) = 0;
        virtual bool hasAudioSource3D(EntityHandle entity) const = 0;
        virtual void playEntityAudio3D(EntityHandle entity, const glm::vec3& position) = 0;
        virtual void pauseEntityAudio3D(EntityHandle entity) = 0;
        virtual void stopEntityAudio3D(EntityHandle entity) = 0;
        virtual void updateEntityAudio3DPosition(EntityHandle entity, const glm::vec3& position) = 0;

        // === Streaming Audio ===

        virtual AudioHandle playStreamingSound(const std::string& path, const AudioParams& params = {}) = 0;

        virtual AudioHandle playStreamingSound3D(const std::string& path, const glm::vec3& position,
                                                  const AudioParams& params = {}) = 0;

        virtual float getPlaybackPosition(AudioHandle handle) const = 0;

        virtual bool setPlaybackPosition(AudioHandle handle, float seconds) = 0;

        virtual float getDuration(AudioHandle handle) const = 0;

        virtual bool isStreamingHandle(AudioHandle handle) const = 0;
    };

}

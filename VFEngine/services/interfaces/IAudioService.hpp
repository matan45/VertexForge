#pragma once
#include "../data/EntityHandle.hpp"
#include <glm/glm.hpp>
#include <string>
#include <optional>

namespace services {

    /**
     * @brief Handle to an active audio source/channel.
     */
    struct AudioHandle {
        uint64_t id = 0;
        bool isValid() const { return id != 0; }
    };

    /**
     * @brief Parameters for playing a sound.
     */
    struct AudioParams {
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;
        bool is3D = false;
        glm::vec3 position{ 0.0f };
        float minDistance = 1.0f;    // Distance at which sound is at full volume
        float maxDistance = 100.0f;  // Distance at which sound is inaudible
        float rolloffFactor = 1.0f;  // How quickly sound attenuates
    };

    /**
     * @brief Data for audio source component.
     */
    struct AudioSourceData {
        std::string clipPath;
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;
        bool playOnStart = false;
        bool is3D = true;
        float minDistance = 1.0f;
        float maxDistance = 100.0f;
    };

    /**
     * @brief Service interface for audio playback operations.
     *
     * This service provides high-level APIs for audio playback
     * using OpenAL under the hood.
     *
     * NOTE: This is an interface stub. Implementations will be added
     * when the audio system is fully integrated.
     */
    class IAudioService {
    public:
        virtual ~IAudioService() = default;

        // === Global Audio Control ===

        /**
         * @brief Set master volume (0.0 - 1.0).
         */
        virtual void setMasterVolume(float volume) = 0;

        /**
         * @brief Get current master volume.
         */
        virtual float getMasterVolume() const = 0;

        /**
         * @brief Pause all audio playback.
         */
        virtual void pauseAll() = 0;

        /**
         * @brief Resume all paused audio.
         */
        virtual void resumeAll() = 0;

        /**
         * @brief Stop all audio playback.
         */
        virtual void stopAll() = 0;

        // === Listener (Camera/Player) ===

        /**
         * @brief Set the audio listener position and orientation.
         */
        virtual void setListenerPosition(const glm::vec3& position,
                                         const glm::vec3& forward,
                                         const glm::vec3& up = glm::vec3(0, 1, 0)) = 0;

        /**
         * @brief Set listener velocity for doppler effect.
         */
        virtual void setListenerVelocity(const glm::vec3& velocity) = 0;

        // === Sound Playback (Fire and Forget) ===

        /**
         * @brief Play a sound effect.
         * @param path Path to the audio file
         * @param params Playback parameters
         * @return Handle to control the sound, or invalid handle on failure
         */
        virtual AudioHandle playSound(const std::string& path, const AudioParams& params = {}) = 0;

        /**
         * @brief Play a sound at a 3D position.
         */
        virtual AudioHandle playSound3D(const std::string& path, const glm::vec3& position,
                                        const AudioParams& params = {}) = 0;

        // === Sound Control ===

        /**
         * @brief Stop a playing sound.
         */
        virtual void stopSound(AudioHandle handle) = 0;

        /**
         * @brief Pause a playing sound.
         */
        virtual void pauseSound(AudioHandle handle) = 0;

        /**
         * @brief Resume a paused sound.
         */
        virtual void resumeSound(AudioHandle handle) = 0;

        /**
         * @brief Check if a sound is currently playing.
         */
        virtual bool isPlaying(AudioHandle handle) const = 0;

        /**
         * @brief Set volume for a specific sound (0.0 - 1.0).
         */
        virtual void setVolume(AudioHandle handle, float volume) = 0;

        /**
         * @brief Set pitch for a specific sound.
         */
        virtual void setPitch(AudioHandle handle, float pitch) = 0;

        /**
         * @brief Set 3D position for a sound.
         */
        virtual void setPosition(AudioHandle handle, const glm::vec3& position) = 0;

        // === Audio Source Component ===

        /**
         * @brief Add an audio source component to an entity.
         */
        virtual void addAudioSource(EntityHandle entity, const AudioSourceData& data) = 0;

        /**
         * @brief Remove audio source from an entity.
         */
        virtual void removeAudioSource(EntityHandle entity) = 0;

        /**
         * @brief Check if entity has an audio source.
         */
        virtual bool hasAudioSource(EntityHandle entity) const = 0;

        /**
         * @brief Play the audio source on an entity.
         */
        virtual void playEntityAudio(EntityHandle entity) = 0;

        /**
         * @brief Stop the audio source on an entity.
         */
        virtual void stopEntityAudio(EntityHandle entity) = 0;
    };

}

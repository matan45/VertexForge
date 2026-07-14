#pragma once
#include "AudioExport.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <unordered_map>

namespace core::audio {

    class ReverbZoneManager;

    class VF_AUDIO_API AudioSceneUpdater {
    public:
        explicit AudioSceneUpdater() = default;
        ~AudioSceneUpdater() = default;

        void setReverbZoneManager(ReverbZoneManager* manager) { reverbZoneManager = manager; }

        void updateListenerFromCamera(const glm::vec3& position,
                                       const glm::vec3& forward,
                                       const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f));

        void updateListenerFromPrimaryCamera();

        // Reverb-only tick: update reverb zones for a given listener position WITHOUT
        // dispatching the listener command. Used by the editor (which dispatches the
        // listener itself from ViewPort) to restore reverb-zone tracking (VK-1505 B4).
        void updateReverbZones(const glm::vec3& listenerPosition);

        // VK-1505: re-sync every playing 3D emitter's transform to the audio thread from
        // its entity's live world transform. dt is reserved for VK-1506 doppler.
        void updateEmitters(float dt);

    private:
        struct EmitterCacheEntry
        {
            std::uint64_t handle = 0;
            glm::vec3 position{0.0f};
            glm::vec3 direction{0.0f};
            bool seenPlaying = false;
        };

        ReverbZoneManager* reverbZoneManager = nullptr;
        std::unordered_map<std::uint64_t, EmitterCacheEntry> emitterCache;
    };

}

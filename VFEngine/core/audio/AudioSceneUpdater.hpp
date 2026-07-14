#pragma once
#include "AudioExport.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <unordered_map>

namespace core::audio {

    class ReverbZoneManager;

    #pragma warning(push)
    #pragma warning(disable: 4251)  // private emitterCache is an impl detail, not DLL ABI
    class VF_AUDIO_API AudioSceneUpdater {
    public:
        explicit AudioSceneUpdater() = default;
        ~AudioSceneUpdater() = default;

        void setReverbZoneManager(ReverbZoneManager* manager) { reverbZoneManager = manager; }

        // VK-1506: teleport-guard speed ceiling for doppler velocity (m/s). Pushed from
        // the authoritative AudioSettings (see AudioSettingsChangedNotification).
        void setMaxDopplerSpeed(float metersPerSecond) { maxDopplerSpeed = metersPerSecond; }

        void updateListenerFromCamera(const glm::vec3& position,
                                       const glm::vec3& forward,
                                       const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f),
                                       float dt = 0.0f);

        void updateListenerFromPrimaryCamera(float dt);

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
            glm::vec3 position{0.0f};           // last DISPATCHED position (dirty-check basis)
            glm::vec3 direction{0.0f};          // last DISPATCHED direction
            glm::vec3 lastFramePosition{0.0f};  // VK-1506: previous-frame position (velocity basis)
            bool hasLastFrame = false;          // VK-1506: false until first frame observed
            bool velocityDispatched = false;    // VK-1506: last dispatch carried non-zero velocity
            bool seenPlaying = false;
        };

        ReverbZoneManager* reverbZoneManager = nullptr;
        std::unordered_map<std::uint64_t, EmitterCacheEntry> emitterCache;

        // VK-1506: doppler teleport-guard ceiling (m/s); speed of sound until first apply.
        float maxDopplerSpeed = 343.3f;

        // VK-1506: runtime listener-velocity basis (editor listener velocity stays zero).
        glm::vec3 lastListenerPosition{0.0f};
        bool hasLastListener = false;
    };
    #pragma warning(pop)

}

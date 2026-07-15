#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace core::audio {

    class ReverbZoneManager;

    // VK-1518: lives in core/audio/ but is compiled into CORE, not the Audio DLL (see the
    // note in premake5.lua). This class is scene->event glue with no OpenAL in it, and it
    // MUST run in the executable: EventDispatcher's singleton comes from the Services
    // StaticLib, so a copy inside Audio.dll would dispatch into a private dispatcher with
    // no handlers registered and silently drop every command. Hence no VF_AUDIO_API here —
    // exporting it would also make Core try to dllimport its own class. Its one cross-DLL
    // call, ReverbZoneManager, is exported from Audio and imported here.
    class AudioSceneUpdater {
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
        //
        // VK-1518: also caches the listener position for occlusion rays. This is the one
        // hook BOTH hosts drive (runtime via updateListenerFromCamera, editor via its
        // CameraPositionUpdatedNotification subscription), which is exactly why it is the
        // right place — see the note on occlusionListenerPosition below.
        void updateReverbZones(const glm::vec3& listenerPosition);

        // VK-1505: re-sync every playing 3D emitter's transform to the audio thread from
        // its entity's live world transform. dt is reserved for VK-1506 doppler.
        void updateEmitters(float dt);

    private:
        // VK-1518, both called once per updateEmitters: cast this frame's budgeted slice of
        // occlusion rays, then push any changed verdict to the audio thread.
        void serveOcclusionRays();
        void dispatchOcclusion();

        struct EmitterCacheEntry
        {
            std::uint64_t handle = 0;
            glm::vec3 position{0.0f};           // last DISPATCHED position (dirty-check basis)
            glm::vec3 direction{0.0f};          // last DISPATCHED direction
            glm::vec3 lastFramePosition{0.0f};  // VK-1506: previous-frame position (velocity basis)
            bool hasLastFrame = false;          // VK-1506: false until first frame observed
            bool velocityDispatched = false;    // VK-1506: last dispatch carried non-zero velocity
            bool seenPlaying = false;

            // VK-1518 geometry occlusion.
            // rayAccum: time owed toward this emitter's next ray. Seeded with a per-entity
            //   phase on first sight so emitters don't all fire on the same frame.
            // occlusionValue / *Amount: this frame's verdict and the authored cut amounts.
            // *Dispatched: last values actually sent. They start at -1, a value the live
            //   fields can never hold, so a new or revived voice always dispatches once
            //   before it can be heard (a recycled pool source starts unoccluded).
            float rayAccum = 0.0f;
            float occlusionValue = 0.0f;
            float lpfAmount = 0.0f;
            float volumeAmount = 0.0f;
            float occlusionDispatched = -1.0f;
            float lpfDispatched = -1.0f;
            float volumeDispatched = -1.0f;
        };

        // VK-1518: an emitter that is due a ray this frame, before the budget cut.
        struct OcclusionCandidate
        {
            std::uint64_t key = 0;
            std::uint64_t handle = 0;
            glm::vec3 position{0.0f};
            float overdue = 0.0f;
            std::uint16_t layerMask = 0;
        };

        // VK-1518: hard ceiling on occlusion raycasts per frame. At 60 fps against the 10 Hz
        // per-emitter cadence this serves ~48 emitters at full rate and degrades gracefully
        // past that (the most-overdue are served first, so nothing starves). Mirrors the
        // private-static budget shape of PhysicsAPI::MAX_RAYCASTS_PER_FRAME; these rays do
        // NOT count against that script-only counter.
        static constexpr int kMaxOcclusionRaysPerFrame = 8;

        ReverbZoneManager* reverbZoneManager = nullptr;
        std::unordered_map<std::uint64_t, EmitterCacheEntry> emitterCache;

        // VK-1518: reused across frames so the per-frame ray scheduling doesn't allocate.
        std::vector<OcclusionCandidate> occlusionScratch;

        // VK-1506: doppler teleport-guard ceiling (m/s); speed of sound until first apply.
        float maxDopplerSpeed = 343.3f;

        // VK-1506: runtime listener-velocity basis (editor listener velocity stays zero).
        glm::vec3 lastListenerPosition{0.0f};
        bool hasLastListener = false;

        // VK-1518: ray origin for occlusion. Deliberately NOT lastListenerPosition — that is
        // the doppler basis and is written only by updateListenerFromCamera, which the EDITOR
        // NEVER CALLS (it dispatches the listener from ViewPort instead; see the note in
        // EditorFrameTaskGraph). Reusing it would leave every editor ray firing from the
        // world origin. updateReverbZones is the one hook both hosts drive, so this is
        // cached there instead.
        glm::vec3 occlusionListenerPosition{0.0f};
        bool hasOcclusionListener = false;
    };

}

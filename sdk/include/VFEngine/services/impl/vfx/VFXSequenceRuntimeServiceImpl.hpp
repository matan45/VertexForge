#pragma once

#include "../../data/VFXSequenceTypes.hpp"
#include "../../data/VFXTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../events/EventDispatcher.hpp"
#include "vfx/VFXSequenceTypes.hpp"
#include "vfx/VFXComboTimeline.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace services
{
    // Owns running VFX combos (VK-1425). A combo is a parent that spawns child
    // VFXInstanceIds from the steps of a .vfVFXSequence asset and forwards
    // play/stop/reset/transform/destroy to them. It is pure orchestration over the
    // existing per-instance vfxruntime command API (dispatched through the
    // EventDispatcher) — no graphics/GPU coupling.
    class VFXSequenceRuntimeServiceImpl
    {
    private:
        struct ActiveStep
        {
            const vfx::VFXSequenceStep* def = nullptr; // points into ComboInstance::data (stable)
            VFXInstanceId childId = 0;
            bool spawned = false;
            bool stopped = false;
            std::optional<vfx::VFXCuePayload> payload;
        };

        struct ComboInstance
        {
            VFXComboInstanceId id = 0;
            std::shared_ptr<const vfx::VFXSequenceData> data;
            glm::mat4 parentTransform{1.0f};
            bool playing = false;
            bool autoDestroyOnFinish = true;
            // Entity used to resolve sockets (combo-level attach and per-step sockets).
            EntityHandle socketEntity;
            bool attached = false;       // whole-combo socket attach
            std::string attachSocket;
            bool socketWarned = false;   // warn-once when a socket can't be resolved
            std::vector<ActiveStep> steps;

            // VK-1451 — deterministic transport state. The simulated clock now lives in
            // `timeline` (timeline.elapsed()); `accumulator` carries the sub-fixedStep
            // remainder so the spawn schedule is independent of frame pacing.
            vfx::VFXComboTimeline timeline;
            uint32_t seed = 0;
            float playbackRate = 1.0f;
            float fixedStep = 0.0f;      // 0 => variable step
            float prewarm = 0.0f;
            float accumulator = 0.0f;
            bool paused = false;
            bool prewarmApplied = false;
        };

        std::unordered_map<VFXComboInstanceId, ComboInstance> combos;
        VFXComboInstanceId nextComboId = 1;
        ::events::SubscriptionToken assetSavedToken;

        // Sequence assets are cached as shared_ptr so each combo's step pointers stay valid.
        std::unordered_map<std::string, std::shared_ptr<const vfx::VFXSequenceData>> sequenceCache;

        std::shared_ptr<const vfx::VFXSequenceData> loadSequence(const std::string& path);
        void invalidateSequence(const std::string& path);
        void spawnStep(ComboInstance& combo, int stepIndex, const glm::mat4& stepParent,
                       const vfx::VFXCuePayload* payload = nullptr);
        void applyComboEvents(ComboInstance& combo, const std::vector<vfx::ComboEvent>& events,
                              const glm::mat4& comboParent, const vfx::VFXCuePayload* manualPayload = nullptr);
        // Rewind + deterministic fixed-step replay of the schedule to `targetSeconds`,
        // then (re)spawn only the steps live at that time. Used by seek and prewarm.
        void replayTo(ComboInstance& combo, float targetSeconds);
        glm::mat4 resolveComboParent(ComboInstance& combo);
        glm::mat4 resolveStepParent(ComboInstance& combo, const ActiveStep& step, const glm::mat4& comboParent);
        glm::mat4 composeStepWorldTransform(const ActiveStep& step, const glm::mat4& stepParent) const;
        void destroyCombo(ComboInstance& combo);
        void publishCueFired(ComboInstance& combo, const std::string& cueName, const vfx::VFXCuePayload& payload);
        void publishNewlyFiredMarkers(ComboInstance& combo, const std::vector<bool>& before);

        static VFXEmitterOverrides toOverrides(const vfx::VFXSequenceStep& step);

    public:
        VFXSequenceRuntimeServiceImpl() = default;
        ~VFXSequenceRuntimeServiceImpl();

        void registerEventHandlers();

        // VK-1451: seed/prewarm/playbackRate/fixedStep are "use asset default" sentinels
        // (seed==0, the floats <0) unless the caller overrides them.
        VFXComboInstanceId createCombo(const std::string& sequenceAssetPath, const glm::mat4& worldTransform,
                                       uint32_t entityId, bool autoDestroyOnFinish,
                                       uint32_t seed = 0, float prewarm = -1.0f,
                                       float playbackRate = -1.0f, float fixedStep = -1.0f);
        void destroyCombo(VFXComboInstanceId id);
        void playCombo(VFXComboInstanceId id);
        void stopCombo(VFXComboInstanceId id);
        void resetCombo(VFXComboInstanceId id);
        void setComboTransform(VFXComboInstanceId id, const glm::mat4& worldTransform);
        void attachComboToSocket(VFXComboInstanceId id, EntityHandle entity, const std::string& socketName);
        void detachCombo(VFXComboInstanceId id);
        void triggerCue(VFXComboInstanceId id, const std::string& cueName,
                        const vfx::VFXCuePayload& payload = {});
        bool isComboPlaying(VFXComboInstanceId id) const;
        void update(float deltaTime);

        // VK-1451 — deterministic transport.
        void setComboPaused(VFXComboInstanceId id, bool paused);
        void setComboPlaybackRate(VFXComboInstanceId id, float rate);
        void seekCombo(VFXComboInstanceId id, float seconds);

        // Destroy every combo and all child instances. Used on exit-play teardown.
        void destroyAll();
    };
}

#pragma once

#include "../../data/VFXSequenceTypes.hpp"
#include "../../data/VFXTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../events/EventDispatcher.hpp"
#include "vfx/VFXSequenceTypes.hpp"
#include <glm/glm.hpp>
#include <memory>
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
        };

        struct ComboInstance
        {
            VFXComboInstanceId id = 0;
            std::shared_ptr<const vfx::VFXSequenceData> data;
            glm::mat4 parentTransform{1.0f};
            float elapsed = 0.0f;
            bool playing = false;
            bool autoDestroyOnFinish = true;
            // Entity used to resolve sockets (combo-level attach and per-step sockets).
            EntityHandle socketEntity;
            bool attached = false;       // whole-combo socket attach
            std::string attachSocket;
            bool socketWarned = false;   // warn-once when a socket can't be resolved
            std::vector<ActiveStep> steps;
        };

        std::unordered_map<VFXComboInstanceId, ComboInstance> combos;
        VFXComboInstanceId nextComboId = 1;
        ::events::SubscriptionToken assetSavedToken;

        // Sequence assets are cached as shared_ptr so each combo's step pointers stay valid.
        std::unordered_map<std::string, std::shared_ptr<const vfx::VFXSequenceData>> sequenceCache;

        std::shared_ptr<const vfx::VFXSequenceData> loadSequence(const std::string& path);
        void invalidateSequence(const std::string& path);
        void spawnStep(ComboInstance& combo, ActiveStep& step, const glm::mat4& stepParent);
        glm::mat4 resolveComboParent(ComboInstance& combo);
        glm::mat4 resolveStepParent(ComboInstance& combo, const ActiveStep& step, const glm::mat4& comboParent);
        void destroyCombo(ComboInstance& combo);

        static glm::mat4 composeStepLocal(const vfx::VFXSequenceStep& step);
        static VFXEmitterOverrides toOverrides(const vfx::VFXSequenceStep& step);

    public:
        VFXSequenceRuntimeServiceImpl() = default;
        ~VFXSequenceRuntimeServiceImpl();

        void registerEventHandlers();

        VFXComboInstanceId createCombo(const std::string& sequenceAssetPath, const glm::mat4& worldTransform,
                                       uint32_t entityId, bool autoDestroyOnFinish);
        void destroyCombo(VFXComboInstanceId id);
        void playCombo(VFXComboInstanceId id);
        void stopCombo(VFXComboInstanceId id);
        void resetCombo(VFXComboInstanceId id);
        void setComboTransform(VFXComboInstanceId id, const glm::mat4& worldTransform);
        void attachComboToSocket(VFXComboInstanceId id, EntityHandle entity, const std::string& socketName);
        void detachCombo(VFXComboInstanceId id);
        void triggerCue(VFXComboInstanceId id, const std::string& cueName);
        bool isComboPlaying(VFXComboInstanceId id) const;
        void update(float deltaTime);

        // Destroy every combo and all child instances. Used on exit-play teardown.
        void destroyAll();
    };
}

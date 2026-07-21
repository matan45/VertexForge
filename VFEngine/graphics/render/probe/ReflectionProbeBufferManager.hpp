#pragma once

// VK-1577 — collects ReflectionProbeComponents from the scene, sorts them into a stable, blend-
// correct order, and uploads them to the probe metadata SSBO bound at set 0 binding 5.
//
// Deliberately does NOT own a descriptor set (unlike FogVolumeBufferManager): the probe buffer is
// bound as part of the shared set-0 IBL layout, so StaticMeshPipeline owns that binding and this
// class only supplies the buffer handle.
//
// The math is not implemented here — it comes from utilities/probe/ReflectionProbeMath.hpp, which
// is doctested. This class is the ECS-to-GPU adapter and nothing more.

#include "ReflectionProbeTypes.hpp"
#include "../../core/VulkanMemoryManager.hpp"

#include <entt/entt.hpp>
#include <vulkan/vulkan.hpp>
#include <array>
#include <vector>

namespace core
{
    class Device;
}

namespace render::probe
{
    // One scene probe that survived culling/capping, in final GPU order.
    struct ResolvedProbe
    {
        entt::entity entity{entt::null};
        uint32_t slot = 0;        // cube slot this probe occupies
        glm::vec3 capturePos{0.0f};
        float nearPlane = 0.1f;
        float farPlane = 100.0f;
        bool captureShadows = false;
        bool dirty = true;        // needs a (re)bake
    };

    class ReflectionProbeBufferManager
    {
    private:
        core::Device& device;

        vk::Buffer probeBuffer;
        core::VulkanAllocation probeAllocation;
        void* mappedMemory = nullptr;

        std::vector<GPUReflectionProbe> cpuProbes;
        std::vector<ResolvedProbe> resolved;

        // A probe only reaches the GPU once its cube actually holds a capture. Until then it is
        // excluded from the uploaded count, so an un-baked probe contributes nothing rather than
        // blending in an empty cube. Phase 4 (the bake) is what sets these.
        std::array<bool, MAX_REFLECTION_PROBES> slotReady{};

        // Which probe each cube slot was last baked FOR. Slots are re-derived from the sort order
        // every frame, so deleting or inserting a probe shifts every probe after it into a
        // different cube. Readiness is a property of (slot, probe) — not of the slot alone — and
        // without this the shifted probe would sample the previous occupant's capture forever.
        // Filled with entt::null by the constructor — value-initializing would give every slot
        // entity 0, which is a VALID handle and would falsely match a real probe.
        std::array<entt::entity, MAX_REFLECTION_PROBES> slotOwner;

        // World position each slot's cube was actually captured FROM. Moving a probe changes the
        // SSBO (so parallax and the blend volume follow immediately) but not the cube contents, so
        // without this a dragged probe keeps reflecting the room it was baked in.
        std::array<glm::vec3, MAX_REFLECTION_PROBES> slotBakedPos{};

        bool initialized = false;

    public:
        explicit ReflectionProbeBufferManager(core::Device& device);
        ~ReflectionProbeBufferManager();

        ReflectionProbeBufferManager(const ReflectionProbeBufferManager&) = delete;
        ReflectionProbeBufferManager& operator=(const ReflectionProbeBufferManager&) = delete;

        void init();
        void cleanup();

        // Walk the registry, sort, and build the CPU-side probe array. Cheap; safe every frame.
        void updateFromScene();
        // Write the header + the READY subset into the mapped buffer.
        void uploadToGPU();

        [[nodiscard]] vk::Buffer getBuffer() const { return probeBuffer; }
        [[nodiscard]] bool isInitialized() const { return initialized; }

        // Probes in final GPU order — what the bake scheduler iterates.
        [[nodiscard]] const std::vector<ResolvedProbe>& getResolvedProbes() const { return resolved; }

        void setSlotReady(uint32_t slot, bool ready)
        {
            if (slot < MAX_REFLECTION_PROBES) slotReady[slot] = ready;
        }
        [[nodiscard]] bool isSlotReady(uint32_t slot) const
        {
            return slot < MAX_REFLECTION_PROBES && slotReady[slot];
        }

        // Records where a slot's cube was captured from — call this when a bake publishes.
        void setSlotBakedPos(uint32_t slot, const glm::vec3& pos)
        {
            if (slot < MAX_REFLECTION_PROBES) slotBakedPos[slot] = pos;
        }
        [[nodiscard]] const glm::vec3& getSlotBakedPos(uint32_t slot) const
        {
            static constexpr glm::vec3 origin{0.0f};
            return slot < MAX_REFLECTION_PROBES ? slotBakedPos[slot] : origin;
        }
        void clearSlotReadiness()
        {
            slotReady = {};
            slotOwner.fill(entt::null);
        }

        // Number of probes actually visible to the shader this frame (ready ones only).
        [[nodiscard]] uint32_t getUploadedCount() const;

    private:
        void createBuffer();
        void destroyBuffer();
    };
}

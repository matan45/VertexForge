#pragma once

#include "../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>
#include <cstdint>

namespace core
{
    class Device;
}

// ============================================================================
// Virtual Texturing (VK-1209) — GPU->CPU page-request feedback. A device-local
// bitmask buffer (one uint per page-table entry) that consuming FRAGMENT shaders
// atomicOr into at their sample point (UV + mip are known there), copied to a
// host-visible staging buffer and read back a frame later. Copy-adapts VSM's
// {Idle,Pending,Ready} readback state machine (VSMFeedbackPipeline) but drops the
// compute generator — VSM reconstructs requests from depth in a compute pass; a
// texture VT gets exact requests inline for free.
//
// The state machine is fence-gated by the caller (clear + copy are recorded into
// the frame's command buffer; readback happens after that frame's fence signals),
// so the single staging buffer is safe — identical to VSM's usage.
// ============================================================================

namespace render::vt
{
    enum class VTFeedbackState : uint8_t
    {
        Idle,
        Pending,
        Ready
    };

    class VTFeedbackReadback
    {
    public:
        explicit VTFeedbackReadback(core::Device& device);
        ~VTFeedbackReadback();

        VTFeedbackReadback(const VTFeedbackReadback&) = delete;
        VTFeedbackReadback& operator=(const VTFeedbackReadback&) = delete;

        void init(uint32_t totalEntries);
        void cleanup();

        // Record: zero the buffer, barrier transfer-write -> fragment read/write.
        void clear(vk::CommandBuffer cmd);
        // Record: barrier fragment-write -> transfer-read, copy device buffer to staging. Sets Pending.
        void copyToStaging(vk::CommandBuffer cmd);
        // Called once the frame that recorded copyToStaging has completed (fence signalled).
        void markReady();
        // Reads the staged bitmask (Ready -> Idle). Empty unless Ready.
        [[nodiscard]] std::vector<uint32_t> readback();

        [[nodiscard]] vk::Buffer getBuffer() const { return feedbackBuffer; }
        [[nodiscard]] uint32_t getTotalEntries() const { return totalEntries; }
        [[nodiscard]] VTFeedbackState getState() const { return state; }
        [[nodiscard]] bool isInitialized() const { return initialized; }

    private:
        void createBuffers();

        core::Device& device;

        vk::Buffer feedbackBuffer;
        core::VulkanAllocation feedbackAllocation;

        vk::Buffer stagingBuffer;
        core::VulkanAllocation stagingAllocation;

        uint32_t totalEntries = 0;
        VTFeedbackState state = VTFeedbackState::Idle;
        bool initialized = false;
    };
}

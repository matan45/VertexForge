#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>
#include <variant>
#include <functional>
#include <cstdint>

namespace core
{
    class Device;

    /**
     * Manages deferred destruction of Vulkan resources to avoid GPU synchronization stalls.
     *
     * Usage pattern:
     * - Call queueXxx() methods to schedule resource destruction
     * - Call processDeletions() once per frame (typically in RenderManager::render())
     * - Resources are destroyed after FRAMES_BEFORE_DELETE frames have passed
     *
     * Thread safety: NOT thread-safe. Call only from main render thread.
     *
     * Cleanup: Call flush() before device destruction to immediately delete all pending resources.
     */
    class DeferredDeletionQueue
    {
    public:
        // Default frames to wait before deletion (MAX_FRAMES_IN_FLIGHT + 1)
        static constexpr uint32_t FRAMES_BEFORE_DELETE = 3;

        explicit DeferredDeletionQueue(Device& device);
        ~DeferredDeletionQueue();

        DeferredDeletionQueue(const DeferredDeletionQueue&) = delete;
        DeferredDeletionQueue& operator=(const DeferredDeletionQueue&) = delete;

        // ========================================
        // Queue resources for deferred deletion
        // ========================================

        // Buffer + memory pair
        void queueBuffer(vk::Buffer buffer, vk::DeviceMemory memory);

        // Image + memory + optional views
        void queueImage(vk::Image image, vk::DeviceMemory memory,
                        const std::vector<vk::ImageView>& views = {});

        // Single image view
        void queueImageView(vk::ImageView view);

        // Framebuffer
        void queueFramebuffer(vk::Framebuffer framebuffer);

        // Sampler
        void queueSampler(vk::Sampler sampler);

        // Descriptor pool (frees all sets allocated from it)
        void queueDescriptorPool(vk::DescriptorPool pool);

        // Generic lambda for custom cleanup
        void queueCustom(std::function<void(vk::Device)> deletionFunc);

        // ========================================
        // Frame management
        // ========================================

        // Call once per frame to process pending deletions
        void processDeletions(uint32_t currentFrame);

        // Flush all pending deletions immediately (call before cleanup)
        void flush();

        // Check if there are pending deletions
        [[nodiscard]] bool hasPendingDeletions() const { return !pendingDeletions.empty(); }

        // Get count of pending deletions
        [[nodiscard]] size_t getPendingCount() const { return pendingDeletions.size(); }

    private:
        Device& device;
        uint32_t lastFrameNumber = 0;

        // Deletion entry types
        struct BufferDeletion
        {
            vk::Buffer buffer;
            vk::DeviceMemory memory;
        };

        struct ImageDeletion
        {
            vk::Image image;
            vk::DeviceMemory memory;
            std::vector<vk::ImageView> views;
        };

        struct ImageViewDeletion
        {
            vk::ImageView view;
        };

        struct FramebufferDeletion
        {
            vk::Framebuffer framebuffer;
        };

        struct SamplerDeletion
        {
            vk::Sampler sampler;
        };

        struct DescriptorPoolDeletion
        {
            vk::DescriptorPool pool;
        };

        struct CustomDeletion
        {
            std::function<void(vk::Device)> deletionFunc;
        };

        using DeletionData = std::variant<
            BufferDeletion,
            ImageDeletion,
            ImageViewDeletion,
            FramebufferDeletion,
            SamplerDeletion,
            DescriptorPoolDeletion,
            CustomDeletion
        >;

        struct PendingDeletion
        {
            DeletionData data;
            uint32_t frameQueued;
        };

        std::vector<PendingDeletion> pendingDeletions;

        void executeDelete(const DeletionData& data);
    };
}

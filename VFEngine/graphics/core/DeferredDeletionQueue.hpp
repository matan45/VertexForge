#pragma once

#include "VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>
#include <variant>
#include <functional>
#include <cstdint>
#include <mutex>

namespace core
{
    class Device;

    class DeferredDeletionQueue
    {
    public:
        static constexpr uint32_t FRAMES_BEFORE_DELETE = 3;

        explicit DeferredDeletionQueue(Device& device);
        ~DeferredDeletionQueue();

        DeferredDeletionQueue(const DeferredDeletionQueue&) = delete;
        DeferredDeletionQueue& operator=(const DeferredDeletionQueue&) = delete;

        void queueBuffer(vk::Buffer buffer, const VulkanAllocation& allocation, VulkanMemoryManager& memManager);
        void queueImage(vk::Image image, const VulkanAllocation& allocation, VulkanMemoryManager& memManager,
                        const std::vector<vk::ImageView>& views = {});
        void queueImageView(vk::ImageView view);
        void queueFramebuffer(vk::Framebuffer framebuffer);
        void queueSampler(vk::Sampler sampler);
        void queueDescriptorPool(vk::DescriptorPool pool);
        void queueCustom(std::function<void(vk::Device)> deletionFunc);

        void processDeletions(uint64_t currentFrame);
        void flush();

        [[nodiscard]] bool hasPendingDeletions() const { std::lock_guard lock(mutex); return !pendingDeletions.empty(); }
        [[nodiscard]] size_t getPendingCount() const { std::lock_guard lock(mutex); return pendingDeletions.size(); }

    private:
        Device& device;
        uint64_t lastFrameNumber = 0;

        struct BufferDeletion
        {
            vk::Buffer buffer;
            VulkanAllocation allocation;
            VulkanMemoryManager* memManager = nullptr;
        };

        struct ImageDeletion
        {
            vk::Image image;
            VulkanAllocation allocation;
            VulkanMemoryManager* memManager = nullptr;
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
            uint64_t frameQueued;
        };

        mutable std::mutex mutex;
        std::vector<PendingDeletion> pendingDeletions;

        void executeDelete(const DeletionData& data);
    };
}

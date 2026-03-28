#include "DeferredDeletionQueue.hpp"
#include "VulkanMemoryManager.hpp"
#include "Device.hpp"
#include <spdlog/spdlog.h>

namespace core
{
    DeferredDeletionQueue::DeferredDeletionQueue(Device& device)
        : device(device)
    {
    }

    DeferredDeletionQueue::~DeferredDeletionQueue()
    {
        flush();
    }

    void DeferredDeletionQueue::queueBuffer(vk::Buffer buffer, const VulkanAllocation& allocation, VulkanMemoryManager& memManager)
    {
        if (!buffer && !allocation.isValid())
            return;

        std::lock_guard lock(mutex);
        pendingDeletions.push_back({
            BufferDeletion{buffer, allocation, &memManager},
            lastFrameNumber
        });
    }

    void DeferredDeletionQueue::queueImage(vk::Image image, const VulkanAllocation& allocation, VulkanMemoryManager& memManager,
                                           const std::vector<vk::ImageView>& views)
    {
        if (!image && !allocation.isValid() && views.empty())
            return;

        std::lock_guard lock(mutex);
        pendingDeletions.push_back({
            ImageDeletion{image, allocation, &memManager, views},
            lastFrameNumber
        });
    }

    void DeferredDeletionQueue::queueImageView(vk::ImageView view)
    {
        if (!view)
            return;

        std::lock_guard lock(mutex);
        pendingDeletions.push_back({
            ImageViewDeletion{view},
            lastFrameNumber
        });
    }

    void DeferredDeletionQueue::queueFramebuffer(vk::Framebuffer framebuffer)
    {
        if (!framebuffer)
            return;

        std::lock_guard lock(mutex);
        pendingDeletions.push_back({
            FramebufferDeletion{framebuffer},
            lastFrameNumber
        });
    }

    void DeferredDeletionQueue::queueSampler(vk::Sampler sampler)
    {
        if (!sampler)
            return;

        std::lock_guard lock(mutex);
        pendingDeletions.push_back({
            SamplerDeletion{sampler},
            lastFrameNumber
        });
    }

    void DeferredDeletionQueue::queueDescriptorPool(vk::DescriptorPool pool)
    {
        if (!pool)
            return;

        std::lock_guard lock(mutex);
        pendingDeletions.push_back({
            DescriptorPoolDeletion{pool},
            lastFrameNumber
        });
    }

    void DeferredDeletionQueue::queueCustom(std::function<void(vk::Device)> deletionFunc)
    {
        if (!deletionFunc)
            return;

        std::lock_guard lock(mutex);
        pendingDeletions.push_back({
            CustomDeletion{std::move(deletionFunc)},
            lastFrameNumber
        });
    }

    void DeferredDeletionQueue::processDeletions(uint32_t currentFrame)
    {
        // Swap pending deletions out under lock, then process without holding lock
        std::vector<PendingDeletion> localDeletions;
        {
            std::lock_guard lock(mutex);
            lastFrameNumber = currentFrame;
            localDeletions.swap(pendingDeletions);
        }

        std::vector<PendingDeletion> kept;
        for (auto& pending : localDeletions)
        {
            uint32_t framesPassed = currentFrame - pending.frameQueued;

            if (framesPassed >= FRAMES_BEFORE_DELETE)
            {
                executeDelete(pending.data);
            }
            else
            {
                kept.push_back(std::move(pending));
            }
        }

        if (!kept.empty())
        {
            std::lock_guard lock(mutex);
            // Prepend kept items before any newly queued items
            kept.insert(kept.end(),
                        std::make_move_iterator(pendingDeletions.begin()),
                        std::make_move_iterator(pendingDeletions.end()));
            pendingDeletions = std::move(kept);
        }
    }

    void DeferredDeletionQueue::flush()
    {
        std::vector<PendingDeletion> localDeletions;
        {
            std::lock_guard lock(mutex);
            localDeletions.swap(pendingDeletions);
        }

        if (localDeletions.empty())
            return;

        device.getLogicalDevice().waitIdle();

        for (const auto& pending : localDeletions)
        {
            executeDelete(pending.data);
        }

        spdlog::debug("DeferredDeletionQueue: Flushed all pending deletions");
    }

    void DeferredDeletionQueue::executeDelete(const DeletionData& data)
    {
        const auto& logicalDevice = device.getLogicalDevice();

        std::visit([&logicalDevice](const auto& deletion) {
            using T = std::decay_t<decltype(deletion)>;

            if constexpr (std::is_same_v<T, BufferDeletion>)
            {
                if (deletion.buffer)
                    logicalDevice.destroyBuffer(deletion.buffer);
                if (deletion.allocation.isValid() && deletion.memManager)
                    deletion.memManager->free(deletion.allocation);
            }
            else if constexpr (std::is_same_v<T, ImageDeletion>)
            {
                for (auto view : deletion.views)
                {
                    if (view)
                        logicalDevice.destroyImageView(view);
                }
                if (deletion.image)
                    logicalDevice.destroyImage(deletion.image);
                if (deletion.allocation.isValid() && deletion.memManager)
                    deletion.memManager->free(deletion.allocation);
            }
            else if constexpr (std::is_same_v<T, ImageViewDeletion>)
            {
                if (deletion.view)
                    logicalDevice.destroyImageView(deletion.view);
            }
            else if constexpr (std::is_same_v<T, FramebufferDeletion>)
            {
                if (deletion.framebuffer)
                    logicalDevice.destroyFramebuffer(deletion.framebuffer);
            }
            else if constexpr (std::is_same_v<T, SamplerDeletion>)
            {
                if (deletion.sampler)
                    logicalDevice.destroySampler(deletion.sampler);
            }
            else if constexpr (std::is_same_v<T, DescriptorPoolDeletion>)
            {
                if (deletion.pool)
                    logicalDevice.destroyDescriptorPool(deletion.pool);
            }
            else if constexpr (std::is_same_v<T, CustomDeletion>)
            {
                if (deletion.deletionFunc)
                    deletion.deletionFunc(logicalDevice);
            }
        }, data);
    }
}

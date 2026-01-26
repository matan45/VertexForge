#include "DeferredDeletionQueue.hpp"
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

    void DeferredDeletionQueue::queueBuffer(vk::Buffer buffer, vk::DeviceMemory memory)
    {
        if (!buffer && !memory)
            return;

        pendingDeletions.push_back({
            BufferDeletion{buffer, memory},
            lastFrameNumber
        });
    }

    void DeferredDeletionQueue::queueImage(vk::Image image, vk::DeviceMemory memory,
                                           const std::vector<vk::ImageView>& views)
    {
        if (!image && !memory && views.empty())
            return;

        pendingDeletions.push_back({
            ImageDeletion{image, memory, views},
            lastFrameNumber
        });
    }

    void DeferredDeletionQueue::queueImageView(vk::ImageView view)
    {
        if (!view)
            return;

        pendingDeletions.push_back({
            ImageViewDeletion{view},
            lastFrameNumber
        });
    }

    void DeferredDeletionQueue::queueFramebuffer(vk::Framebuffer framebuffer)
    {
        if (!framebuffer)
            return;

        pendingDeletions.push_back({
            FramebufferDeletion{framebuffer},
            lastFrameNumber
        });
    }

    void DeferredDeletionQueue::queueSampler(vk::Sampler sampler)
    {
        if (!sampler)
            return;

        pendingDeletions.push_back({
            SamplerDeletion{sampler},
            lastFrameNumber
        });
    }

    void DeferredDeletionQueue::queueDescriptorPool(vk::DescriptorPool pool)
    {
        if (!pool)
            return;

        pendingDeletions.push_back({
            DescriptorPoolDeletion{pool},
            lastFrameNumber
        });
    }

    void DeferredDeletionQueue::queueCustom(std::function<void(vk::Device)> deletionFunc)
    {
        if (!deletionFunc)
            return;

        pendingDeletions.push_back({
            CustomDeletion{std::move(deletionFunc)},
            lastFrameNumber
        });
    }

    void DeferredDeletionQueue::processDeletions(uint32_t currentFrame)
    {
        lastFrameNumber = currentFrame;

        // Process deletions that are old enough
        auto it = pendingDeletions.begin();
        while (it != pendingDeletions.end())
        {
            uint32_t framesPassed = currentFrame - it->frameQueued;

            if (framesPassed >= FRAMES_BEFORE_DELETE)
            {
                executeDelete(it->data);
                it = pendingDeletions.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void DeferredDeletionQueue::flush()
    {
        if (pendingDeletions.empty())
            return;

        // Wait for GPU to finish all work before flushing
        device.getLogicalDevice().waitIdle();

        for (const auto& pending : pendingDeletions)
        {
            executeDelete(pending.data);
        }

        pendingDeletions.clear();
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
                if (deletion.memory)
                    logicalDevice.freeMemory(deletion.memory);
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
                if (deletion.memory)
                    logicalDevice.freeMemory(deletion.memory);
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

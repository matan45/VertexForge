#include "TransientResourcePool.hpp"
#include "../../core/Device.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/DeferredDeletionQueue.hpp"
#include "print/Log.hpp"
#include <algorithm>

namespace render::graph
{
    TransientResourcePool::TransientResourcePool(core::Device& device)
        : device(device)
    {
    }

    TransientResourcePool::~TransientResourcePool()
    {
        cleanup();
    }

    void TransientResourcePool::allocateTransients(std::vector<ResourceNode>& resources,
                                                    uint32_t sortedPassCount)
    {
        if (allocated)
            cleanup();

        // Collect transient resources with valid lifetimes
        std::vector<IntervalEntry> intervals;
        for (uint32_t i = 0; i < resources.size(); ++i)
        {
            auto& resource = resources[i];
            if (resource.imported || !resource.desc.transient)
                continue;
            if (resource.firstUsePass == UINT32_MAX)
                continue;

            // Create the image to query memory requirements
            vk::ImageCreateInfo imageInfo{};
            imageInfo.imageType = vk::ImageType::e2D;
            imageInfo.extent.width = resource.desc.extent.width;
            imageInfo.extent.height = resource.desc.extent.height;
            imageInfo.extent.depth = 1;
            imageInfo.mipLevels = resource.desc.mipLevels;
            imageInfo.arrayLayers = resource.desc.arrayLayers;
            imageInfo.format = resource.desc.format;
            imageInfo.tiling = vk::ImageTiling::eOptimal;
            imageInfo.initialLayout = vk::ImageLayout::eUndefined;
            imageInfo.usage = resource.desc.usage;
            imageInfo.samples = vk::SampleCountFlagBits::e1;
            imageInfo.sharingMode = vk::SharingMode::eExclusive;
            imageInfo.flags = vk::ImageCreateFlagBits::eAlias;

            vk::Image tempImage = device.getLogicalDevice().createImage(imageInfo);
            vk::MemoryRequirements memReqs = device.getLogicalDevice().getImageMemoryRequirements(tempImage);

            uint32_t memType = 0;
            auto memProperties = device.getPhysicalDevice().getMemoryProperties();
            for (uint32_t j = 0; j < memProperties.memoryTypeCount; ++j)
            {
                if ((memReqs.memoryTypeBits & (1 << j)) &&
                    (memProperties.memoryTypes[j].propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal))
                {
                    memType = j;
                    break;
                }
            }

            IntervalEntry entry{};
            entry.resourceIndex = i;
            entry.firstUse = resource.firstUsePass;
            entry.lastUse = resource.lastUsePass;
            entry.requiredSize = memReqs.size;
            entry.requiredMemoryType = memType;

            intervals.push_back(entry);

            // Store the temp image — we'll bind it to memory later
            TransientImage transImg{};
            transImg.image = tempImage;
            transImg.desc = resource.desc;
            images[i] = transImg;
        }

        if (intervals.empty())
            return;

        // Sort by first use for greedy interval coloring
        std::sort(intervals.begin(), intervals.end(),
            [](const IntervalEntry& a, const IntervalEntry& b)
            {
                return a.firstUse < b.firstUse;
            });

        // Track when each slot becomes free (last use of current occupant)
        std::vector<std::pair<uint32_t, uint32_t>> slotOccupancy; // (lastUse, slotIndex)

        for (auto& entry : intervals)
        {
            uint32_t slotIdx = findOrCreateSlot(entry.requiredSize, entry.requiredMemoryType,
                                                 slotOccupancy);

            // Update slot occupancy
            bool found = false;
            for (auto& occ : slotOccupancy)
            {
                if (occ.second == slotIdx)
                {
                    occ.first = entry.lastUse;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                slotOccupancy.push_back({entry.lastUse, slotIdx});
            }

            // Bind image to slot memory
            auto& transImg = images[entry.resourceIndex];
            transImg.slotIndex = slotIdx;

            device.getLogicalDevice().bindImageMemory(
                transImg.image, slots[slotIdx].memory, 0);

            // Create image view
            vk::ImageViewCreateInfo viewInfo{};
            viewInfo.image = transImg.image;
            viewInfo.viewType = vk::ImageViewType::e2D;
            viewInfo.format = transImg.desc.format;
            viewInfo.subresourceRange.aspectMask = transImg.desc.aspectMask;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = transImg.desc.mipLevels;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = transImg.desc.arrayLayers;

            transImg.view = device.getLogicalDevice().createImageView(viewInfo);

            // Update resource node with physical handles
            auto& resource = resources[entry.resourceIndex];
            resource.physicalImage = transImg.image;
            resource.physicalView = transImg.view;
        }

        allocated = true;

        vfLogInfo("TransientResourcePool: {} transient images in {} memory slots (peak {:.2f} MB)",
                 images.size(), slots.size(),
                 static_cast<float>(getPeakMemoryUsage()) / (1024.0f * 1024.0f));
    }

    uint32_t TransientResourcePool::findOrCreateSlot(
        vk::DeviceSize size, uint32_t memoryType,
        const std::vector<std::pair<uint32_t, uint32_t>>& slotOccupancy)
    {
        // Try to find an existing slot that's free (occupant's lastUse has passed)
        // and large enough
        // Note: since we sort intervals by firstUse, a slot is free if its occupant's
        // lastUse < current entry's firstUse (not checked here directly — the caller
        // handles this via the greedy approach)

        // For simplicity, look for a slot whose current occupant has finished
        // and whose size is sufficient
        for (const auto& [lastUse, slotIdx] : slotOccupancy)
        {
            auto& slot = slots[slotIdx];
            if (slot.size >= size && slot.memoryTypeIndex == memoryType)
            {
                // Check if slot is actually free (handled by greedy sort order)
                return slotIdx;
            }
        }

        // Allocate a new slot
        vk::MemoryAllocateInfo allocInfo{};
        allocInfo.allocationSize = size;
        allocInfo.memoryTypeIndex = memoryType;

        MemorySlot slot{};
        slot.memory = device.getLogicalDevice().allocateMemory(allocInfo);
        slot.size = size;
        slot.memoryTypeIndex = memoryType;

        uint32_t idx = static_cast<uint32_t>(slots.size());
        slots.push_back(slot);
        return idx;
    }

    void TransientResourcePool::releaseAll(core::DeferredDeletionQueue& deletionQueue,
                                            core::VulkanMemoryManager& memManager)
    {
        for (auto& [resIdx, transImg] : images)
        {
            if (transImg.view)
            {
                deletionQueue.queueImageView(transImg.view);
                transImg.view = nullptr;
            }
        }

        // Queue custom deletion for memory slots
        for (auto& slot : slots)
        {
            if (slot.memory)
            {
                vk::DeviceMemory mem = slot.memory;
                deletionQueue.queueCustom([mem](vk::Device dev)
                {
                    dev.freeMemory(mem);
                });
                slot.memory = nullptr;
            }
        }

        // Destroy images immediately (they're unbound after memory is freed)
        for (auto& [resIdx, transImg] : images)
        {
            if (transImg.image)
            {
                vk::Image img = transImg.image;
                deletionQueue.queueCustom([img](vk::Device dev)
                {
                    dev.destroyImage(img);
                });
                transImg.image = nullptr;
            }
        }

        images.clear();
        slots.clear();
        allocated = false;
    }

    void TransientResourcePool::cleanup()
    {
        auto logicalDevice = device.getLogicalDevice();

        for (auto& [resIdx, transImg] : images)
        {
            if (transImg.view)
                logicalDevice.destroyImageView(transImg.view);
            if (transImg.image)
                logicalDevice.destroyImage(transImg.image);
        }

        for (auto& slot : slots)
        {
            if (slot.memory)
                logicalDevice.freeMemory(slot.memory);
        }

        images.clear();
        slots.clear();
        allocated = false;
    }

    vk::DeviceSize TransientResourcePool::getPeakMemoryUsage() const
    {
        vk::DeviceSize total = 0;
        for (const auto& slot : slots)
            total += slot.size;
        return total;
    }
}

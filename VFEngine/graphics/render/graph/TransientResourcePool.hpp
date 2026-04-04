#pragma once

#include "RenderGraphTypes.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include <vector>
#include <unordered_map>
#include <memory>

namespace core
{
    class Device;
    class DeferredDeletionQueue;
}

namespace render::graph
{
    // A physical memory slot that can be shared by multiple transient images
    // with non-overlapping lifetimes
    struct MemorySlot
    {
        vk::DeviceMemory memory{};
        vk::DeviceSize size = 0;
        uint32_t memoryTypeIndex = 0;

        // Track which resource currently "owns" this slot for aliasing barrier insertion
        uint32_t currentOwnerResource = UINT32_MAX;
    };

    // A physical image bound to a memory slot
    struct TransientImage
    {
        vk::Image image{};
        vk::ImageView view{};
        uint32_t slotIndex = UINT32_MAX;
        ImageResourceDesc desc{};
    };

    // Manages transient render targets with memory aliasing via interval-graph coloring
    class TransientResourcePool
    {
    public:
        explicit TransientResourcePool(core::Device& device);
        ~TransientResourcePool();

        TransientResourcePool(const TransientResourcePool&) = delete;
        TransientResourcePool& operator=(const TransientResourcePool&) = delete;

        // Allocate transient resources based on compiled resource lifetimes.
        // resources: the graph's resource nodes (only transient ones are processed)
        // sortedPassCount: total number of passes in the sorted order
        void allocateTransients(std::vector<ResourceNode>& resources, uint32_t sortedPassCount);

        // Release all transient allocations (e.g., on swapchain resize)
        // Uses DeferredDeletionQueue for safe cleanup
        void releaseAll(core::DeferredDeletionQueue& deletionQueue, core::VulkanMemoryManager& memManager);

        // Clean up immediately (for shutdown)
        void cleanup();

        uint32_t getSlotCount() const { return static_cast<uint32_t>(slots.size()); }
        uint32_t getImageCount() const { return static_cast<uint32_t>(images.size()); }

        // Peak VRAM used by all transient slots
        vk::DeviceSize getPeakMemoryUsage() const;

    private:
        struct IntervalEntry
        {
            uint32_t resourceIndex;
            uint32_t firstUse;
            uint32_t lastUse;
            vk::DeviceSize requiredSize;
            uint32_t requiredMemoryType;
        };

        uint32_t findOrCreateSlot(vk::DeviceSize size, uint32_t memoryType,
                                   const std::vector<std::pair<uint32_t, uint32_t>>& slotOccupancy);

        core::Device& device;

        std::vector<MemorySlot> slots;
        std::unordered_map<uint32_t, TransientImage> images; // keyed by resource index
        bool allocated = false;
    };
}

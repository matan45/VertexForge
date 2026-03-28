#pragma once

#include "SVTTypes.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>
#include <cstdint>

namespace core
{
    class Device;
}

namespace render::svt
{
    // CPU mirror of the SVT page table, with SSBO upload to GPU.
    // The page table maps virtual tile coordinates to physical tile indices.
    class SVTPageTable
    {
    private:
        core::Device& device;
        SVTConfig config;

        // CPU-side page table entries (all mip levels, packed sequentially)
        std::vector<SVTPageTableEntry> entries;
        uint32_t totalEntries = 0;

        // Per-mip metadata
        struct MipInfo
        {
            uint32_t offset = 0;        // Offset into entries array
            uint32_t tilesPerSide = 0;  // Number of tiles per side at this mip
        };
        std::vector<MipInfo> mipInfos;
        uint32_t mipLevelCount = 0;

        // GPU SSBO
        vk::Buffer buffer;
        core::VulkanAllocation allocation;
        void* mapped = nullptr;  // Persistently mapped (host-visible + coherent)

        bool initialized = false;
        bool dirty = false;

    public:
        explicit SVTPageTable(core::Device& device);
        ~SVTPageTable();

        SVTPageTable(const SVTPageTable&) = delete;
        SVTPageTable& operator=(const SVTPageTable&) = delete;

        void init(const SVTConfig& config);
        void cleanup();

        // Set a page table entry for a virtual tile
        void setEntry(const VirtualTileCoord& coord, const SVTPageTableEntry& entry);

        // Clear (invalidate) a page table entry
        void clearEntry(const VirtualTileCoord& coord);

        // Get a page table entry
        const SVTPageTableEntry& getEntry(const VirtualTileCoord& coord) const;

        // Flush dirty entries to GPU (memcpy to mapped SSBO)
        void flushToGPU();

        // Clear all entries (mark all as invalid)
        void clearAll();

        // Get the flat index for a virtual tile coordinate
        uint32_t getFlatIndex(const VirtualTileCoord& coord) const;

        vk::Buffer getBuffer() const { return buffer; }
        uint32_t getTotalEntries() const { return totalEntries; }
        uint32_t getMipLevelCount() const { return mipLevelCount; }

        uint32_t getTilesPerSide(uint32_t mipLevel) const
        {
            return mipLevel < mipLevelCount ? mipInfos[mipLevel].tilesPerSide : 0;
        }

        uint32_t getMipOffset(uint32_t mipLevel) const
        {
            return mipLevel < mipLevelCount ? mipInfos[mipLevel].offset : 0;
        }

        bool isInitialized() const { return initialized; }

    private:
        void createBuffer();
    };
}

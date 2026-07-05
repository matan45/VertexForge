#pragma once

#include "VTTypes.hpp"
#include "VTPoolAllocator.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>
#include <cstdint>

namespace core
{
    class Device;
}

// ============================================================================
// Virtual Texturing (VK-1209) — physical page atlas ("pool"). Copy-adapted from
// VSMPhysicalTilePool but format-parametrized and multi-plane: an RVT pool is two
// RGBA8 planes (albedo+emission / ORM+flags) rendered as an MRT bake target; an
// SVT pool is one BC7 plane uploaded via transfer. All planes share one tile
// index (and one page table). Tile allocation delegates to the pure, unit-tested
// VTPoolAllocator. Depth-specific bits from VSM (D32 format, comparison sampler,
// depth aspect, dual-layer copy) are dropped.
// ============================================================================

namespace render::vt
{
    struct VTPoolDesc
    {
        uint32_t poolDim = 4096;                    // atlas edge in texels (multiple of VT_PAGE_SIZE)
        std::vector<vk::Format> planeFormats;       // one entry per MRT plane
        vk::ImageUsageFlags usage =
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
        bool enableAnisotropy = false;              // requires the samplerAnisotropy device feature
    };

    class VTPhysicalPool
    {
    public:
        explicit VTPhysicalPool(core::Device& device);
        ~VTPhysicalPool();

        VTPhysicalPool(const VTPhysicalPool&) = delete;
        VTPhysicalPool& operator=(const VTPhysicalPool&) = delete;

        void init(const VTPoolDesc& desc);
        void cleanup();

        // Tile allocation (delegates to VTPoolAllocator).
        [[nodiscard]] uint32_t allocateTile() { return allocator.allocate(); }
        bool freeTile(uint32_t tile) { return allocator.free(tile); }
        void freeAllTiles() { allocator.freeAll(); }
        [[nodiscard]] uint32_t freeTileCount() const { return allocator.freeCount(); }
        [[nodiscard]] uint32_t allocatedTileCount() const { return allocator.allocatedCount(); }
        [[nodiscard]] uint32_t maxTiles() const { return allocator.capacityCount(); }
        [[nodiscard]] float utilization() const;

        // Tile geometry within the atlas (matches VTTypes addressing).
        [[nodiscard]] vk::Viewport getTileViewport(uint32_t tile) const;
        [[nodiscard]] vk::Rect2D getTileScissor(uint32_t tile) const;
        // Region to copy a full 128x128 tile from a staging buffer into `plane` (SVT upload).
        [[nodiscard]] vk::BufferImageCopy getTileBufferCopy(uint32_t tile, uint32_t plane,
                                                            vk::DeviceSize bufferOffset) const;

        [[nodiscard]] uint32_t planeCount() const { return static_cast<uint32_t>(planes.size()); }
        [[nodiscard]] vk::Image planeImage(uint32_t i) const { return planes[i].image; }
        [[nodiscard]] vk::ImageView planeView(uint32_t i) const { return planes[i].view; }
        [[nodiscard]] vk::Format planeFormat(uint32_t i) const { return planes[i].format; }
        [[nodiscard]] vk::Sampler getSampler() const { return sampler; }
        [[nodiscard]] uint32_t getPoolDim() const { return poolDim; }
        [[nodiscard]] uint32_t getTilesPerSide() const { return vtTilesPerSide(poolDim); }
        [[nodiscard]] bool isInitialized() const { return initialized; }

        // Whole-image layout transitions across all planes.
        void transition(vk::CommandBuffer cmd,
                        vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
                        vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage,
                        vk::AccessFlags srcAccess, vk::AccessFlags dstAccess) const;

    private:
        struct Plane
        {
            vk::Image image;
            core::VulkanAllocation allocation;
            vk::ImageView view;
            vk::Format format = vk::Format::eR8G8B8A8Unorm;
        };

        void createPlanes();
        void createSampler();

        core::Device& device;
        VTPoolDesc desc;
        uint32_t poolDim = 0;
        std::vector<Plane> planes;
        vk::Sampler sampler;
        VTPoolAllocator allocator;
        bool initialized = false;
    };
}

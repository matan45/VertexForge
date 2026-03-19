#pragma once

#include "VSMTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>
#include <cstdint>

namespace core
{
    class Device;
}

namespace render::shadow
{
    class VSMPhysicalTilePool
    {
    private:
        core::Device& device;

        vk::Image poolImage;
        vk::DeviceMemory poolMemory;
        vk::ImageView poolImageView;

        vk::Sampler comparisonSampler;
        vk::Sampler depthSampler;

        vk::RenderPass renderPass;          // eClear
        vk::RenderPass renderPassLoad;      // eLoad (preserves existing depth)
        vk::Framebuffer framebuffer;
        vk::Framebuffer framebufferLoad;

        std::vector<uint32_t> freeTiles;

        vk::Format depthFormat = vk::Format::eD32Sfloat;
        bool initialized = false;

    public:
        explicit VSMPhysicalTilePool(core::Device& device);
        ~VSMPhysicalTilePool();

        VSMPhysicalTilePool(const VSMPhysicalTilePool&) = delete;
        VSMPhysicalTilePool& operator=(const VSMPhysicalTilePool&) = delete;

        void init();
        void cleanup();

        [[nodiscard]] uint32_t allocateTile();
        void freeTile(uint32_t tileIndex);
        void freeAllTiles();

        [[nodiscard]] vk::Viewport getTileViewport(uint32_t tileIndex) const;
        [[nodiscard]] vk::Rect2D getTileScissor(uint32_t tileIndex) const;

        [[nodiscard]] vk::Image getPoolImage() const { return poolImage; }
        [[nodiscard]] vk::ImageView getPoolImageView() const { return poolImageView; }
        [[nodiscard]] vk::Sampler getComparisonSampler() const { return comparisonSampler; }
        [[nodiscard]] vk::Sampler getDepthSampler() const { return depthSampler; }
        [[nodiscard]] vk::RenderPass getRenderPass() const { return renderPass; }
        [[nodiscard]] vk::RenderPass getRenderPassLoad() const { return renderPassLoad; }
        [[nodiscard]] vk::Framebuffer getFramebuffer() const { return framebuffer; }
        [[nodiscard]] vk::Framebuffer getFramebufferLoad() const { return framebufferLoad; }
        [[nodiscard]] vk::Format getDepthFormat() const { return depthFormat; }

        [[nodiscard]] uint32_t getFreeTileCount() const { return static_cast<uint32_t>(freeTiles.size()); }
        [[nodiscard]] uint32_t getAllocatedTileCount() const { return vsm::MAX_PHYSICAL_TILES - static_cast<uint32_t>(freeTiles.size()); }
        [[nodiscard]] float getUtilization() const;
        [[nodiscard]] bool isInitialized() const { return initialized; }

        // Dual-layer tile copy support
        [[nodiscard]] vk::ImageCopy getTileCopyRegion(uint32_t srcTileIndex, uint32_t dstTileIndex) const;
        static void transitionPoolToTransfer(vk::CommandBuffer cmd, VSMPhysicalTilePool* tilePool);
        static void transitionPoolFromTransfer(vk::CommandBuffer cmd, VSMPhysicalTilePool* tilePool);

    private:
        void createPoolImage();
        void createSamplers();
        void createRenderPasses();
        void createFramebuffers();
    };
}

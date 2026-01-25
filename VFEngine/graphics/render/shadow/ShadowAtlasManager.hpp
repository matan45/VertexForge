#pragma once

#include "ShadowTypes.hpp"
#include "types/RenderSettings.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>
#include <unordered_map>

namespace core
{
    class Device;
    class SwapChain;
}

namespace render::shadow
{
    /**
     * Manages shadow map atlas allocation and GPU resources.
     *
     * Uses a simple grid-based allocator for fixed-size tiles.
     * The atlas is a single large depth texture where individual
     * shadow maps are allocated as tiles.
     */
    class ShadowAtlasManager
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        // Atlas dimensions
        uint32_t atlasWidth = ShadowConstants::DEFAULT_ATLAS_SIZE;
        uint32_t atlasHeight = ShadowConstants::DEFAULT_ATLAS_SIZE;

        // GPU resources
        vk::Image atlasImage;
        vk::DeviceMemory atlasMemory;
        vk::ImageView atlasImageView;           // Full atlas view for sampling
        vk::Sampler atlasSampler;               // Standard sampler
        vk::Sampler comparisonSampler;          // For hardware PCF

        // Descriptor resources
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        // Tile allocation tracking
        std::vector<ShadowAtlasTile> tiles;
        std::unordered_map<uint32_t, uint32_t> handleToTileIndex;  // atlasIndex -> tiles vector index

        // Entity-to-handles tracking for bulk operations
        std::unordered_map<uint32_t, std::vector<uint32_t>> entityToHandles;  // entityId -> atlasIndex list

        // State
        bool initialized = false;
        uint32_t nextAtlasIndex = 0;

        // Depth format for shadow maps
        vk::Format depthFormat = vk::Format::eD32Sfloat;

    public:
        explicit ShadowAtlasManager(core::Device& device, core::SwapChain& swapChain);
        ~ShadowAtlasManager();

        ShadowAtlasManager(const ShadowAtlasManager&) = delete;
        ShadowAtlasManager& operator=(const ShadowAtlasManager&) = delete;

        void init(uint32_t width = ShadowConstants::DEFAULT_ATLAS_SIZE,
                  uint32_t height = ShadowConstants::DEFAULT_ATLAS_SIZE);
        void cleanup();
        void recreate();

        // Allocation API
        [[nodiscard]] ShadowMapHandle allocate(uint32_t width, uint32_t height,
                                               ShadowMapType type, uint32_t layer = 0);
        void free(const ShadowMapHandle& handle);
        void freeAll();

        // Batch allocation for cascades (atomic success/rollback)
        [[nodiscard]] std::vector<ShadowMapHandle> allocateCascades(
            uint32_t resolution, uint32_t cascadeCount, uint32_t lightEntityId);

        // Entity-based bulk operations
        void freeAllForEntity(uint32_t entityId);
        void trackHandleForEntity(uint32_t entityId, uint32_t atlasIndex);

        // Dynamic resize (waits for GPU idle, reallocates existing tiles)
        void resize(uint32_t newWidth, uint32_t newHeight);

        // Apply quality settings (may trigger resize)
        void applyQualitySettings(const types::ShadowAtlasConfig& config);

        // Query allocated region
        [[nodiscard]] ShadowAtlasTile getTile(const ShadowMapHandle& handle) const;
        [[nodiscard]] glm::vec4 getNormalizedViewport(const ShadowMapHandle& handle) const;

        // Get pixel viewport for rendering
        [[nodiscard]] vk::Viewport getPixelViewport(const ShadowMapHandle& handle) const;
        [[nodiscard]] vk::Rect2D getScissorRect(const ShadowMapHandle& handle) const;

        // Accessors
        [[nodiscard]] vk::Image getAtlasImage() const { return atlasImage; }
        [[nodiscard]] vk::ImageView getAtlasImageView() const { return atlasImageView; }
        [[nodiscard]] vk::Sampler getAtlasSampler() const { return atlasSampler; }
        [[nodiscard]] vk::Sampler getComparisonSampler() const { return comparisonSampler; }
        [[nodiscard]] vk::DescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
        [[nodiscard]] vk::DescriptorSet getDescriptorSet() const { return descriptorSet; }
        [[nodiscard]] vk::Format getDepthFormat() const { return depthFormat; }

        [[nodiscard]] uint32_t getAtlasWidth() const { return atlasWidth; }
        [[nodiscard]] uint32_t getAtlasHeight() const { return atlasHeight; }
        [[nodiscard]] bool isInitialized() const { return initialized; }

        // Statistics
        [[nodiscard]] uint32_t getAllocatedTileCount() const;
        [[nodiscard]] float getAtlasUtilization() const;

    private:
        void createAtlasImage();
        void createAtlasSamplers();
        void createDescriptorResources();
        void updateDescriptorSet();

        [[nodiscard]] int32_t findFreeTileSlot(uint32_t width, uint32_t height) const;
        [[nodiscard]] bool canFitTile(uint32_t x, uint32_t y, uint32_t width, uint32_t height) const;
    };
}

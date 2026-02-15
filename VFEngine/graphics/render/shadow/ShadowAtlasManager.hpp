#pragma once

#include "ShadowTypes.hpp"
#include "types/RenderSettings.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>
#include <unordered_map>

namespace core
{
    class Device;
}

namespace render::shadow
{
    class ShadowAtlasManager
    {
    private:
        core::Device& device;

        uint32_t atlasWidth = ShadowConstants::DEFAULT_ATLAS_SIZE;
        uint32_t atlasHeight = ShadowConstants::DEFAULT_ATLAS_SIZE;

        vk::Image atlasImage;
        vk::DeviceMemory atlasMemory;
        vk::ImageView atlasImageView;
        vk::Sampler atlasSampler;
        vk::Sampler comparisonSampler;

        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        std::vector<ShadowAtlasTile> tiles;
        std::unordered_map<uint32_t, uint32_t> handleToTileIndex;
        std::unordered_map<uint32_t, std::vector<uint32_t>> entityToHandles;

        bool initialized = false;
        uint32_t nextAtlasIndex = 0;
        vk::Format depthFormat = vk::Format::eD32Sfloat;

    public:
        explicit ShadowAtlasManager(core::Device& device);
        ~ShadowAtlasManager();

        ShadowAtlasManager(const ShadowAtlasManager&) = delete;
        ShadowAtlasManager& operator=(const ShadowAtlasManager&) = delete;

        void init(uint32_t width = ShadowConstants::DEFAULT_ATLAS_SIZE,
                  uint32_t height = ShadowConstants::DEFAULT_ATLAS_SIZE);
        void cleanup();

        [[nodiscard]] ShadowMapHandle allocate(uint32_t width, uint32_t height,
                                               ShadowMapType type, uint32_t layer = 0);
        void free(const ShadowMapHandle& handle);
        void freeAll();

        [[nodiscard]] std::vector<ShadowMapHandle> allocateCascades(
            uint32_t resolution, uint32_t cascadeCount, uint32_t lightEntityId);

        void freeAllForEntity(uint32_t entityId);
        void trackHandleForEntity(uint32_t entityId, uint32_t atlasIndex);

        struct ResizeResult
        {
            bool success = false;
            uint32_t requestedCount = 0;
            uint32_t reallocatedCount = 0;
            bool allReallocated() const { return requestedCount == reallocatedCount; }
        };

        [[nodiscard]] ResizeResult resize(uint32_t newWidth, uint32_t newHeight);
        [[nodiscard]] ResizeResult applyQualitySettings(const types::ShadowAtlasConfig& config);

        [[nodiscard]] ShadowAtlasTile getTile(const ShadowMapHandle& handle) const;
        [[nodiscard]] glm::vec4 getNormalizedViewport(const ShadowMapHandle& handle) const;
        [[nodiscard]] vk::Viewport getPixelViewport(const ShadowMapHandle& handle) const;
        [[nodiscard]] vk::Rect2D getScissorRect(const ShadowMapHandle& handle) const;

        [[nodiscard]] vk::Image getAtlasImage() const { return atlasImage; }
        [[nodiscard]] vk::ImageView getAtlasImageView() const { return atlasImageView; }
        [[nodiscard]] vk::Sampler getComparisonSampler() const { return comparisonSampler; }
        [[nodiscard]] vk::DescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
        [[nodiscard]] vk::DescriptorSet getDescriptorSet() const { return descriptorSet; }
        [[nodiscard]] vk::Format getDepthFormat() const { return depthFormat; }

        [[nodiscard]] uint32_t getAtlasWidth() const { return atlasWidth; }
        [[nodiscard]] uint32_t getAtlasHeight() const { return atlasHeight; }
        [[nodiscard]] bool isInitialized() const { return initialized; }

        [[nodiscard]] float getAtlasUtilization() const;

    private:
        void createAtlasImage();
        void createAtlasSamplers();
        void createDescriptorResources();
        void updateDescriptorSet();
        void destroyAtlasResources();
        void transitionAtlasToShaderRead();

        [[nodiscard]] bool canFitTile(uint32_t x, uint32_t y, uint32_t width, uint32_t height) const;
    };
}

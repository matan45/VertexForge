#include "ShadowAtlasManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/ImageUtilities.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace render::shadow
{
    ShadowAtlasManager::ShadowAtlasManager(core::Device& device, core::SwapChain& swapChain)
        : device(device)
        , swapChain(swapChain)
    {
    }

    ShadowAtlasManager::~ShadowAtlasManager()
    {
        cleanup();
    }

    void ShadowAtlasManager::init(uint32_t width, uint32_t height)
    {
        if (initialized)
        {
            spdlog::warn("ShadowAtlasManager::init() called when already initialized");
            return;
        }

        atlasWidth = width;
        atlasHeight = height;

        // Use D32_SFLOAT for shadow maps - good precision and widely supported
        depthFormat = vk::Format::eD32Sfloat;

        createAtlasImage();
        createAtlasSamplers();
        createDescriptorResources();
        updateDescriptorSet();

        initialized = true;
        spdlog::info("ShadowAtlasManager initialized with {}x{} atlas", atlasWidth, atlasHeight);
    }

    void ShadowAtlasManager::cleanup()
    {
        if (!initialized)
            return;

        const auto& logicalDevice = device.getLogicalDevice();

        // Wait for device to be idle before cleanup
        logicalDevice.waitIdle();

        // Cleanup descriptor resources
        if (descriptorPool)
        {
            logicalDevice.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }
        if (descriptorSetLayout)
        {
            logicalDevice.destroyDescriptorSetLayout(descriptorSetLayout);
            descriptorSetLayout = nullptr;
        }

        // Cleanup samplers
        if (comparisonSampler)
        {
            logicalDevice.destroySampler(comparisonSampler);
            comparisonSampler = nullptr;
        }
        if (atlasSampler)
        {
            logicalDevice.destroySampler(atlasSampler);
            atlasSampler = nullptr;
        }

        // Cleanup image view
        if (atlasImageView)
        {
            logicalDevice.destroyImageView(atlasImageView);
            atlasImageView = nullptr;
        }

        // Cleanup image and memory
        if (atlasImage)
        {
            logicalDevice.destroyImage(atlasImage);
            atlasImage = nullptr;
        }
        if (atlasMemory)
        {
            logicalDevice.freeMemory(atlasMemory);
            atlasMemory = nullptr;
        }

        // Clear allocation data
        tiles.clear();
        handleToTileIndex.clear();
        nextAtlasIndex = 0;

        initialized = false;
        spdlog::info("ShadowAtlasManager cleaned up");
    }

    void ShadowAtlasManager::recreate()
    {
        uint32_t savedWidth = atlasWidth;
        uint32_t savedHeight = atlasHeight;

        cleanup();
        init(savedWidth, savedHeight);
    }

    void ShadowAtlasManager::createAtlasImage()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        // Create depth image for shadow atlas
        core::ImageInfoRequest imageInfo(
            logicalDevice,
            physicalDevice,
            atlasWidth,
            atlasHeight,
            1,  // layers
            1,  // mipLevels
            depthFormat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );

        core::ImageUtilities::createImage(imageInfo, atlasImage, atlasMemory);

        // Create image view
        core::ImageViewInfoRequest viewInfo(
            logicalDevice,
            atlasImage,
            depthFormat,
            vk::ImageAspectFlagBits::eDepth,
            vk::ImageViewType::e2D,
            1,  // layerCount
            1   // mipLevels
        );

        core::ImageUtilities::createImageView(viewInfo, atlasImageView);
    }

    void ShadowAtlasManager::createAtlasSamplers()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        // Standard sampler for reading shadow map values
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToBorder;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToBorder;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToBorder;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = vk::CompareOp::eNever;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;
        samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;  // White = max depth = no shadow
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        atlasSampler = logicalDevice.createSampler(samplerInfo);

        // Comparison sampler for hardware PCF
        vk::SamplerCreateInfo comparisonInfo = samplerInfo;
        comparisonInfo.compareEnable = VK_TRUE;
        comparisonInfo.compareOp = vk::CompareOp::eLessOrEqual;

        comparisonSampler = logicalDevice.createSampler(comparisonInfo);
    }

    void ShadowAtlasManager::createDescriptorResources()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        // Descriptor set layout - shadow atlas texture + comparison sampler
        std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};

        // Binding 0: Shadow atlas sampled image
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

        // Binding 1: Shadow atlas with comparison sampler (for hardware PCF)
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = logicalDevice.createDescriptorSetLayout(layoutInfo);

        // Descriptor pool
        std::array<vk::DescriptorPoolSize, 1> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[0].descriptorCount = 2;  // One for each binding

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        descriptorPool = logicalDevice.createDescriptorPool(poolInfo);

        // Allocate descriptor set
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        descriptorSet = logicalDevice.allocateDescriptorSets(allocInfo)[0];
    }

    void ShadowAtlasManager::updateDescriptorSet()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        vk::DescriptorImageInfo atlasImageInfo{};
        atlasImageInfo.sampler = atlasSampler;
        atlasImageInfo.imageView = atlasImageView;
        atlasImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::DescriptorImageInfo comparisonImageInfo{};
        comparisonImageInfo.sampler = comparisonSampler;
        comparisonImageInfo.imageView = atlasImageView;
        comparisonImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        std::array<vk::WriteDescriptorSet, 2> writes{};

        writes[0].dstSet = descriptorSet;
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &atlasImageInfo;

        writes[1].dstSet = descriptorSet;
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &comparisonImageInfo;

        logicalDevice.updateDescriptorSets(writes, nullptr);
    }

    ShadowMapHandle ShadowAtlasManager::allocate(uint32_t width, uint32_t height,
                                                  ShadowMapType type, uint32_t layer)
    {
        ShadowMapHandle handle;
        handle.type = type;
        handle.layer = layer;

        // Simple row-based allocation strategy
        // Find a position where we can fit this tile
        uint32_t bestX = 0;
        uint32_t bestY = 0;
        bool found = false;

        // Try to find space in existing rows or start a new row
        uint32_t currentRowY = 0;
        uint32_t currentRowHeight = 0;
        uint32_t currentX = 0;

        // Collect existing tile positions to avoid overlap
        for (const auto& tile : tiles)
        {
            if (!tile.allocated)
                continue;

            // Track the maximum Y extent
            uint32_t tileEndY = tile.y + tile.height;
            if (tileEndY > currentRowY)
            {
                currentRowY = tileEndY;
            }
        }

        // Try placing at the end of existing rows first
        for (uint32_t y = 0; y + height <= atlasHeight; y += 256)  // Step by 256 for alignment
        {
            for (uint32_t x = 0; x + width <= atlasWidth; x += 256)
            {
                if (canFitTile(x, y, width, height))
                {
                    bestX = x;
                    bestY = y;
                    found = true;
                    break;
                }
            }
            if (found) break;
        }

        if (!found)
        {
            spdlog::warn("ShadowAtlasManager: Failed to allocate {}x{} tile - atlas full", width, height);
            handle.invalidate();
            return handle;
        }

        // Create the tile
        ShadowAtlasTile tile;
        tile.x = bestX;
        tile.y = bestY;
        tile.width = width;
        tile.height = height;
        tile.layer = layer;
        tile.allocated = true;

        handle.atlasIndex = nextAtlasIndex++;
        tile.owner = handle;

        // Store tile
        uint32_t tileIndex = static_cast<uint32_t>(tiles.size());
        tiles.push_back(tile);
        handleToTileIndex[handle.atlasIndex] = tileIndex;

        spdlog::debug("ShadowAtlasManager: Allocated tile {} at ({}, {}) size {}x{}",
                      handle.atlasIndex, bestX, bestY, width, height);

        return handle;
    }

    bool ShadowAtlasManager::canFitTile(uint32_t x, uint32_t y, uint32_t width, uint32_t height) const
    {
        // Check atlas bounds
        if (x + width > atlasWidth || y + height > atlasHeight)
            return false;

        // Check overlap with existing tiles
        for (const auto& tile : tiles)
        {
            if (!tile.allocated)
                continue;

            // Check for intersection
            bool overlapX = (x < tile.x + tile.width) && (x + width > tile.x);
            bool overlapY = (y < tile.y + tile.height) && (y + height > tile.y);

            if (overlapX && overlapY)
                return false;
        }

        return true;
    }

    void ShadowAtlasManager::free(const ShadowMapHandle& handle)
    {
        if (!handle.isValid())
            return;

        auto it = handleToTileIndex.find(handle.atlasIndex);
        if (it == handleToTileIndex.end())
        {
            spdlog::warn("ShadowAtlasManager: Attempted to free unknown handle {}", handle.atlasIndex);
            return;
        }

        uint32_t tileIndex = it->second;
        if (tileIndex < tiles.size())
        {
            tiles[tileIndex].allocated = false;
            tiles[tileIndex].owner.invalidate();
        }

        handleToTileIndex.erase(it);
        spdlog::debug("ShadowAtlasManager: Freed tile {}", handle.atlasIndex);
    }

    void ShadowAtlasManager::freeAll()
    {
        tiles.clear();
        handleToTileIndex.clear();
        nextAtlasIndex = 0;
        spdlog::debug("ShadowAtlasManager: Freed all tiles");
    }

    ShadowAtlasTile ShadowAtlasManager::getTile(const ShadowMapHandle& handle) const
    {
        if (!handle.isValid())
            return ShadowAtlasTile{};

        auto it = handleToTileIndex.find(handle.atlasIndex);
        if (it == handleToTileIndex.end())
            return ShadowAtlasTile{};

        return tiles[it->second];
    }

    glm::vec4 ShadowAtlasManager::getNormalizedViewport(const ShadowMapHandle& handle) const
    {
        auto tile = getTile(handle);
        if (!tile.allocated)
            return glm::vec4(0.0f);

        return tile.getNormalizedViewport(atlasWidth, atlasHeight);
    }

    vk::Viewport ShadowAtlasManager::getPixelViewport(const ShadowMapHandle& handle) const
    {
        auto tile = getTile(handle);
        if (!tile.allocated)
            return vk::Viewport{0, 0, 1, 1, 0.0f, 1.0f};

        return vk::Viewport{
            static_cast<float>(tile.x),
            static_cast<float>(tile.y),
            static_cast<float>(tile.width),
            static_cast<float>(tile.height),
            0.0f,
            1.0f
        };
    }

    vk::Rect2D ShadowAtlasManager::getScissorRect(const ShadowMapHandle& handle) const
    {
        auto tile = getTile(handle);
        if (!tile.allocated)
            return vk::Rect2D{{0, 0}, {1, 1}};

        return vk::Rect2D{
            {static_cast<int32_t>(tile.x), static_cast<int32_t>(tile.y)},
            {tile.width, tile.height}
        };
    }

    uint32_t ShadowAtlasManager::getAllocatedTileCount() const
    {
        uint32_t count = 0;
        for (const auto& tile : tiles)
        {
            if (tile.allocated)
                ++count;
        }
        return count;
    }

    float ShadowAtlasManager::getAtlasUtilization() const
    {
        uint32_t usedPixels = 0;
        for (const auto& tile : tiles)
        {
            if (tile.allocated)
                usedPixels += tile.width * tile.height;
        }
        return static_cast<float>(usedPixels) / static_cast<float>(atlasWidth * atlasHeight);
    }

    int32_t ShadowAtlasManager::findFreeTileSlot(uint32_t width, uint32_t height) const
    {
        // Look for an existing freed slot that can fit this size
        for (size_t i = 0; i < tiles.size(); ++i)
        {
            const auto& tile = tiles[i];
            if (!tile.allocated && tile.width >= width && tile.height >= height)
            {
                return static_cast<int32_t>(i);
            }
        }
        return -1;
    }

    std::vector<ShadowMapHandle> ShadowAtlasManager::allocateCascades(
        uint32_t resolution, uint32_t cascadeCount, uint32_t lightEntityId)
    {
        std::vector<ShadowMapHandle> handles;
        handles.reserve(cascadeCount);

        for (uint32_t i = 0; i < cascadeCount; ++i)
        {
            ShadowMapHandle handle = allocate(resolution, resolution, ShadowMapType::DirectionalCSM, i);
            if (!handle.isValid())
            {
                // Rollback all previous allocations
                for (const auto& h : handles)
                {
                    free(h);
                }
                handles.clear();
                spdlog::error("ShadowAtlasManager: Failed to allocate cascade {} for entity {}", i, lightEntityId);
                return handles;
            }

            handle.cascadeIndex = static_cast<uint16_t>(i);
            handles.push_back(handle);

            // Track this handle for the entity
            trackHandleForEntity(lightEntityId, handle.atlasIndex);
        }

        spdlog::debug("ShadowAtlasManager: Allocated {} cascades for entity {}", cascadeCount, lightEntityId);
        return handles;
    }

    void ShadowAtlasManager::trackHandleForEntity(uint32_t entityId, uint32_t atlasIndex)
    {
        entityToHandles[entityId].push_back(atlasIndex);
    }

    void ShadowAtlasManager::freeAllForEntity(uint32_t entityId)
    {
        auto it = entityToHandles.find(entityId);
        if (it == entityToHandles.end())
            return;

        for (uint32_t atlasIndex : it->second)
        {
            auto tileIt = handleToTileIndex.find(atlasIndex);
            if (tileIt != handleToTileIndex.end())
            {
                uint32_t tileIndex = tileIt->second;
                if (tileIndex < tiles.size())
                {
                    tiles[tileIndex].allocated = false;
                    tiles[tileIndex].owner.invalidate();
                }
                handleToTileIndex.erase(tileIt);
            }
        }

        spdlog::debug("ShadowAtlasManager: Freed {} tiles for entity {}", it->second.size(), entityId);
        entityToHandles.erase(it);
    }

    void ShadowAtlasManager::resize(uint32_t newWidth, uint32_t newHeight)
    {
        if (!initialized)
        {
            spdlog::warn("ShadowAtlasManager::resize() called when not initialized");
            return;
        }

        if (newWidth == atlasWidth && newHeight == atlasHeight)
        {
            spdlog::debug("ShadowAtlasManager: Resize skipped - same dimensions");
            return;
        }

        spdlog::info("ShadowAtlasManager: Resizing atlas from {}x{} to {}x{}",
                     atlasWidth, atlasHeight, newWidth, newHeight);

        const auto& logicalDevice = device.getLogicalDevice();

        // Wait for GPU to finish using current resources
        logicalDevice.waitIdle();

        // Store current allocation info for reallocation
        struct AllocationInfo
        {
            uint32_t width;
            uint32_t height;
            ShadowMapType type;
            uint32_t layer;
            uint32_t atlasIndex;
        };
        std::vector<AllocationInfo> allocations;

        for (const auto& tile : tiles)
        {
            if (tile.allocated)
            {
                allocations.push_back({
                    tile.width,
                    tile.height,
                    tile.owner.type,
                    tile.layer,
                    tile.owner.atlasIndex
                });
            }
        }

        // Cleanup existing image resources (but not descriptor layout/pool)
        if (atlasImageView)
        {
            logicalDevice.destroyImageView(atlasImageView);
            atlasImageView = nullptr;
        }
        if (atlasImage)
        {
            logicalDevice.destroyImage(atlasImage);
            atlasImage = nullptr;
        }
        if (atlasMemory)
        {
            logicalDevice.freeMemory(atlasMemory);
            atlasMemory = nullptr;
        }

        // Clear allocation data
        tiles.clear();
        handleToTileIndex.clear();
        // Note: entityToHandles is cleared - caller must re-track if needed
        entityToHandles.clear();
        nextAtlasIndex = 0;

        // Update dimensions
        atlasWidth = newWidth;
        atlasHeight = newHeight;

        // Recreate image with new size
        createAtlasImage();

        // Update descriptor set with new image view
        updateDescriptorSet();

        // Reallocate previous tiles (may fail if new size is smaller)
        uint32_t reallocated = 0;
        for (const auto& info : allocations)
        {
            ShadowMapHandle handle = allocate(info.width, info.height, info.type, info.layer);
            if (handle.isValid())
            {
                ++reallocated;
            }
            else
            {
                spdlog::warn("ShadowAtlasManager: Failed to reallocate tile {}x{} after resize",
                             info.width, info.height);
            }
        }

        spdlog::info("ShadowAtlasManager: Resize complete - reallocated {}/{} tiles",
                     reallocated, allocations.size());
    }

    void ShadowAtlasManager::applyQualitySettings(const types::ShadowAtlasConfig& config)
    {
        if (config.atlasSize == 0)
        {
            spdlog::debug("ShadowAtlasManager: Quality Off - atlas disabled");
            return;
        }

        if (config.atlasSize != atlasWidth || config.atlasSize != atlasHeight)
        {
            resize(config.atlasSize, config.atlasSize);
        }
    }
}

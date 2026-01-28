#include "ShadowAtlasManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/Utilities.hpp"
#include "print/Logger.hpp"
#include <algorithm>

namespace render::shadow
{
    ShadowAtlasManager::ShadowAtlasManager(core::Device& device)
        : device(device)
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
            loggerWarning("ShadowAtlasManager::init() called when already initialized");
            return;
        }

        atlasWidth = width;
        atlasHeight = height;
        depthFormat = vk::Format::eD32Sfloat;

        createAtlasImage();
        createAtlasSamplers();
        createDescriptorResources();

        const auto& logicalDevice = device.getLogicalDevice();
        auto cmd = core::Utilities::beginSingleTimeCommands(logicalDevice, device.getStagingCommandPool());

        vk::ImageMemoryBarrier barrier{};
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.oldLayout = vk::ImageLayout::eUndefined;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = atlasImage;
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        cmd->pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eFragmentShader,
            {},
            0, nullptr,
            0, nullptr,
            1, &barrier
        );

        core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), cmd, nullptr);

        updateDescriptorSet();

        initialized = true;
    }

    void ShadowAtlasManager::cleanup()
    {
        if (!initialized)
            return;

        const auto& logicalDevice = device.getLogicalDevice();
        logicalDevice.waitIdle();

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

        tiles.clear();
        handleToTileIndex.clear();
        nextAtlasIndex = 0;

        initialized = false;
    }

    void ShadowAtlasManager::createAtlasImage()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        core::ImageInfoRequest imageInfo(
            logicalDevice,
            physicalDevice,
            atlasWidth,
            atlasHeight,
            1,
            1,
            depthFormat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );

        core::ImageUtilities::createImage(imageInfo, atlasImage, atlasMemory);

        core::ImageViewInfoRequest viewInfo(
            logicalDevice,
            atlasImage,
            depthFormat,
            vk::ImageAspectFlagBits::eDepth,
            vk::ImageViewType::e2D,
            1,
            1
        );

        core::ImageUtilities::createImageView(viewInfo, atlasImageView);
    }

    void ShadowAtlasManager::createAtlasSamplers()
    {
        const auto& logicalDevice = device.getLogicalDevice();

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
        samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        atlasSampler = logicalDevice.createSampler(samplerInfo);

        vk::SamplerCreateInfo comparisonInfo = samplerInfo;
        comparisonInfo.compareEnable = VK_TRUE;
        comparisonInfo.compareOp = vk::CompareOp::eLessOrEqual;

        comparisonSampler = logicalDevice.createSampler(comparisonInfo);
    }

    void ShadowAtlasManager::createDescriptorResources()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};

        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = logicalDevice.createDescriptorSetLayout(layoutInfo);

        std::array<vk::DescriptorPoolSize, 1> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[0].descriptorCount = 2;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        descriptorPool = logicalDevice.createDescriptorPool(poolInfo);

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

        uint32_t bestX = 0;
        uint32_t bestY = 0;
        bool found = false;

        uint32_t currentRowY = 0;

        for (const auto& tile : tiles)
        {
            if (!tile.allocated)
                continue;

            uint32_t tileEndY = tile.y + tile.height;
            if (tileEndY > currentRowY)
                currentRowY = tileEndY;
        }

        for (uint32_t y = 0; y + height <= atlasHeight; y += 256)
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
            loggerWarning("ShadowAtlasManager: Failed to allocate {}x{} tile - atlas full", width, height);
            handle.invalidate();
            return handle;
        }

        ShadowAtlasTile tile;
        tile.x = bestX;
        tile.y = bestY;
        tile.width = width;
        tile.height = height;
        tile.layer = layer;
        tile.allocated = true;

        handle.atlasIndex = nextAtlasIndex++;
        tile.owner = handle;

        uint32_t tileIndex = static_cast<uint32_t>(tiles.size());
        tiles.push_back(tile);
        handleToTileIndex[handle.atlasIndex] = tileIndex;

        return handle;
    }

    bool ShadowAtlasManager::canFitTile(uint32_t x, uint32_t y, uint32_t width, uint32_t height) const
    {
        if (x + width > atlasWidth || y + height > atlasHeight)
            return false;

        for (const auto& tile : tiles)
        {
            if (!tile.allocated)
                continue;

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
            loggerWarning("ShadowAtlasManager: Attempted to free unknown handle {}", handle.atlasIndex);
            return;
        }

        uint32_t tileIndex = it->second;
        if (tileIndex < tiles.size())
        {
            tiles[tileIndex].allocated = false;
            tiles[tileIndex].owner.invalidate();
        }

        handleToTileIndex.erase(it);
    }

    void ShadowAtlasManager::freeAll()
    {
        tiles.clear();
        handleToTileIndex.clear();
        nextAtlasIndex = 0;
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

    float ShadowAtlasManager::getAtlasUtilization() const
    {
        uint64_t usedPixels = 0;
        for (const auto& tile : tiles)
        {
            if (tile.allocated)
                usedPixels += static_cast<uint64_t>(tile.width) * tile.height;
        }
        const uint64_t totalPixels = static_cast<uint64_t>(atlasWidth) * atlasHeight;
        return static_cast<float>(usedPixels) / static_cast<float>(totalPixels);
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
                for (const auto& h : handles)
                    free(h);
                handles.clear();
                loggerError("ShadowAtlasManager: Failed to allocate cascade {} for entity {}", i, lightEntityId);
                return handles;
            }

            handle.cascadeIndex = static_cast<uint16_t>(i);
            handles.push_back(handle);
            trackHandleForEntity(lightEntityId, handle.atlasIndex);
        }

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

        entityToHandles.erase(it);
    }

    ShadowAtlasManager::ResizeResult ShadowAtlasManager::resize(uint32_t newWidth, uint32_t newHeight)
    {
        ResizeResult result;

        if (!initialized)
        {
            loggerWarning("ShadowAtlasManager::resize() called when not initialized");
            return result;
        }

        if (newWidth == atlasWidth && newHeight == atlasHeight)
        {
            result.success = true;
            return result;
        }

        const auto& logicalDevice = device.getLogicalDevice();
        logicalDevice.waitIdle();

        std::unordered_map<uint32_t, uint32_t> atlasIndexToEntity;
        for (const auto& [entityId, indices] : entityToHandles)
        {
            for (uint32_t atlasIndex : indices)
                atlasIndexToEntity[atlasIndex] = entityId;
        }

        struct AllocationInfo
        {
            uint32_t width;
            uint32_t height;
            ShadowMapType type;
            uint32_t layer;
            uint32_t oldAtlasIndex;
            uint32_t entityId;
        };
        std::vector<AllocationInfo> allocations;

        for (const auto& tile : tiles)
        {
            if (tile.allocated)
            {
                uint32_t entityId = 0;
                auto entityIt = atlasIndexToEntity.find(tile.owner.atlasIndex);
                if (entityIt != atlasIndexToEntity.end())
                    entityId = entityIt->second;

                allocations.push_back({
                    tile.width,
                    tile.height,
                    tile.owner.type,
                    tile.layer,
                    tile.owner.atlasIndex,
                    entityId
                });
            }
        }

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

        tiles.clear();
        handleToTileIndex.clear();
        entityToHandles.clear();
        nextAtlasIndex = 0;

        atlasWidth = newWidth;
        atlasHeight = newHeight;

        createAtlasImage();

        auto cmd = core::Utilities::beginSingleTimeCommands(logicalDevice, device.getStagingCommandPool());

        vk::ImageMemoryBarrier barrier{};
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.oldLayout = vk::ImageLayout::eUndefined;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = atlasImage;
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        cmd->pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eFragmentShader,
            {},
            0, nullptr,
            0, nullptr,
            1, &barrier
        );

        core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), cmd, nullptr);

        updateDescriptorSet();

        result.requestedCount = static_cast<uint32_t>(allocations.size());
        result.reallocatedCount = 0;

        for (const auto& info : allocations)
        {
            ShadowMapHandle handle = allocate(info.width, info.height, info.type, info.layer);
            if (handle.isValid())
            {
                ++result.reallocatedCount;

                if (info.entityId != 0)
                    trackHandleForEntity(info.entityId, handle.atlasIndex);
            }
            else
            {
                loggerWarning("ShadowAtlasManager: Failed to reallocate tile {}x{} after resize",
                             info.width, info.height);
            }
        }

        result.success = true;

        if (!result.allReallocated())
        {
            loggerWarning("ShadowAtlasManager: Resize complete with partial failure - {}/{} tiles reallocated",
                         result.reallocatedCount, result.requestedCount);
        }

        return result;
    }

    ShadowAtlasManager::ResizeResult ShadowAtlasManager::applyQualitySettings(const types::ShadowAtlasConfig& config)
    {
        ResizeResult result;
        result.success = true;

        if (config.atlasSize == 0)
            return result;

        if (config.atlasSize != atlasWidth || config.atlasSize != atlasHeight)
            return resize(config.atlasSize, config.atlasSize);

        return result;
    }
}

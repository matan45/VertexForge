#include "PhysicalTileCache.hpp"
#include "../../core/Device.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <cstring>

namespace render::svt
{
    PhysicalTileCache::PhysicalTileCache(core::Device& device)
        : device(device)
    {
    }

    PhysicalTileCache::~PhysicalTileCache()
    {
        cleanup();
    }

    void PhysicalTileCache::init(const SVTConfig& cfg)
    {
        if (initialized) return;
        config = cfg;

        // Create all 5 channel caches
        createChannelCache(albedoCache, vk::Format::eBc7SrgbBlock, "SVT_Albedo");
        createChannelCache(normalCache, vk::Format::eBc7UnormBlock, "SVT_Normal");
        createChannelCache(ormCache, vk::Format::eBc7UnormBlock, "SVT_ORM");
        createChannelCache(emissionCache, vk::Format::eBc7SrgbBlock, "SVT_Emission");
        createChannelCache(heightCache, vk::Format::eBc7UnormBlock, "SVT_Height");

        // Initialize tile tracking
        tileSlots.resize(config.physicalTileCount);
        freeList.reserve(config.physicalTileCount);
        for (uint32_t i = config.physicalTileCount; i > 0; --i)
        {
            freeList.push_back(i - 1);
        }

        createStagingBuffer();

        // Create command pool for upload commands
        auto queueFamily = device.getQueueFamilyIndices().graphicsAndComputeFamily.value();
        vk::CommandPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer
                       | vk::CommandPoolCreateFlagBits::eTransient;
        poolInfo.queueFamilyIndex = queueFamily;
        commandPool = device.getLogicalDevice().createCommandPool(poolInfo);

        initialized = true;
        vfLogInfo("SVT PhysicalTileCache initialized: {} tiles per channel, {}x{} physical tile size",
                     config.physicalTileCount, SVT_PHYSICAL_TILE_SIZE, SVT_PHYSICAL_TILE_SIZE);
    }

    void PhysicalTileCache::cleanup()
    {
        if (!initialized) return;

        auto dev = device.getLogicalDevice();
        dev.waitIdle();

        if (commandPool)
        {
            dev.destroyCommandPool(commandPool);
            commandPool = nullptr;
        }

        if (stagingBuffer)
        {
            if (stagingMapped)
            {
                dev.unmapMemory(stagingMemory);
                stagingMapped = nullptr;
            }
            core::BufferUtilities::destroyBuffer(dev, stagingBuffer, stagingMemory);
        }

        destroyChannelCache(albedoCache);
        destroyChannelCache(normalCache);
        destroyChannelCache(ormCache);
        destroyChannelCache(emissionCache);
        destroyChannelCache(heightCache);

        tileSlots.clear();
        freeList.clear();
        initialized = false;
    }

    uint32_t PhysicalTileCache::allocateTile()
    {
        if (freeList.empty()) return SVT_INVALID_TILE;

        uint32_t idx = freeList.back();
        freeList.pop_back();
        tileSlots[idx].occupied = true;
        return idx;
    }

    void PhysicalTileCache::freeTile(uint32_t tileIndex)
    {
        if (tileIndex >= config.physicalTileCount) return;
        tileSlots[tileIndex] = {};
        freeList.push_back(tileIndex);
    }

    uint32_t PhysicalTileCache::evictLRU(uint64_t currentFrame)
    {
        uint32_t bestIdx = SVT_INVALID_TILE;
        uint64_t oldestFrame = currentFrame;

        for (uint32_t i = 0; i < config.physicalTileCount; ++i)
        {
            auto& slot = tileSlots[i];
            if (slot.occupied && slot.lastUsedFrame < oldestFrame)
            {
                oldestFrame = slot.lastUsedFrame;
                bestIdx = i;
            }
        }

        if (bestIdx != SVT_INVALID_TILE)
        {
            tileSlots[bestIdx] = {};
            // Don't push to freeList — caller will reuse immediately
        }

        return bestIdx;
    }

    void PhysicalTileCache::touchTile(uint32_t tileIndex, uint64_t frame)
    {
        if (tileIndex < config.physicalTileCount)
        {
            tileSlots[tileIndex].lastUsedFrame = frame;
        }
    }

    void PhysicalTileCache::setTileMapping(uint32_t tileIndex, const VirtualTileCoord& virtualCoord)
    {
        if (tileIndex < config.physicalTileCount)
        {
            tileSlots[tileIndex].virtualCoord = virtualCoord;
        }
    }

    void PhysicalTileCache::uploadTileData(uint32_t tileIndex, uint32_t channelIndex,
                                           const void* compressedData, uint32_t dataSize)
    {
        if (tileIndex >= config.physicalTileCount || !compressedData) return;

        switch (channelIndex)
        {
        case SVT_CHANNEL_ALBEDO:   uploadToLayer(albedoCache, tileIndex, compressedData, dataSize); break;
        case SVT_CHANNEL_NORMAL:   uploadToLayer(normalCache, tileIndex, compressedData, dataSize); break;
        case SVT_CHANNEL_ORM:      uploadToLayer(ormCache, tileIndex, compressedData, dataSize); break;
        case SVT_CHANNEL_EMISSION: uploadToLayer(emissionCache, tileIndex, compressedData, dataSize); break;
        case SVT_CHANNEL_HEIGHT:   uploadToLayer(heightCache, tileIndex, compressedData, dataSize); break;
        default: break;
        }
    }

    void PhysicalTileCache::uploadTileBatched(uint32_t tileIndex,
                                               const ChannelUploadData channels[SVT_CHANNEL_COUNT])
    {
        if (tileIndex >= config.physicalTileCount) return;

        ChannelCache* caches[] = { &albedoCache, &normalCache, &ormCache, &emissionCache, &heightCache };

        // Copy all channel data into staging buffer at offsets
        size_t totalCopied = 0;
        for (uint32_t i = 0; i < SVT_CHANNEL_COUNT; ++i)
        {
            if (!channels[i].data || channels[i].size == 0) continue;
            if (totalCopied + channels[i].size > stagingBufferSize) break;

            std::memcpy(static_cast<uint8_t*>(stagingMapped) + i * SVT_TILE_SIZE_BC7,
                        channels[i].data, channels[i].size);
            totalCopied += channels[i].size;
        }

        auto dev = device.getLogicalDevice();

        vk::CommandBufferAllocateInfo allocInfo{};
        allocInfo.commandPool = commandPool;
        allocInfo.level = vk::CommandBufferLevel::ePrimary;
        allocInfo.commandBufferCount = 1;
        auto cmdBuf = dev.allocateCommandBuffers(allocInfo)[0];

        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        cmdBuf.begin(beginInfo);

        for (uint32_t i = 0; i < SVT_CHANNEL_COUNT; ++i)
        {
            if (!channels[i].data || channels[i].size == 0) continue;

            prepareUploadBarrier(cmdBuf, *caches[i], tileIndex);

            vk::BufferImageCopy region{};
            region.bufferOffset = i * SVT_TILE_SIZE_BC7;
            region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            region.imageSubresource.baseArrayLayer = tileIndex;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = vk::Extent3D{SVT_PHYSICAL_TILE_SIZE, SVT_PHYSICAL_TILE_SIZE, 1};

            cmdBuf.copyBufferToImage(stagingBuffer, caches[i]->image,
                                     vk::ImageLayout::eTransferDstOptimal, region);

            finalizeUploadBarrier(cmdBuf, *caches[i], tileIndex);
        }

        cmdBuf.end();

        vk::SubmitInfo submitInfo{};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuf;
        device.submitGraphics(submitInfo);
        device.waitGraphicsIdle();

        dev.freeCommandBuffers(commandPool, cmdBuf);
    }

    void PhysicalTileCache::flushUploads()
    {
        // Channel uploads are batched per-tile in uploadTileBatched() (1 submit per tile).
        // No additional flush needed at this level.
    }

    // ---- Private ----

    void PhysicalTileCache::createChannelCache(ChannelCache& cache, vk::Format format, const char* debugName)
    {
        cache.format = format;

        createCacheImage(cache, format);
        createCacheSampler(cache);
        transitionCacheLayout(cache);

        vfLogInfo("SVT: Created {} cache: {}x{}x{} layers, format {}",
                      debugName, SVT_PHYSICAL_TILE_SIZE, SVT_PHYSICAL_TILE_SIZE,
                      config.physicalTileCount, static_cast<int>(format));
    }

    void PhysicalTileCache::createCacheImage(ChannelCache& cache, vk::Format format)
    {
        auto dev = device.getLogicalDevice();
        auto physDev = device.getPhysicalDevice();

        // Create 2D array image: each layer is one physical tile
        core::ImageInfoRequest imgReq(dev, physDev,
            SVT_PHYSICAL_TILE_SIZE,
            SVT_PHYSICAL_TILE_SIZE,
            config.physicalTileCount,  // array layers = tile count
            1,                          // single mip per tile layer
            format,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::ImageUtilities::createImage(imgReq, cache.image, cache.memory);

        // Create image view as 2D array
        core::ImageViewInfoRequest viewReq(dev, cache.image,
            format,
            vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2DArray,
            config.physicalTileCount,
            1
        );
        core::ImageUtilities::createImageView(viewReq, cache.view);
    }

    void PhysicalTileCache::createCacheSampler(ChannelCache& cache)
    {
        auto dev = device.getLogicalDevice();

        // Create sampler with linear filtering and clamp-to-edge (tiles have borders for filtering)
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = 16.0f;
        samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;
        cache.sampler = dev.createSampler(samplerInfo);
    }

    void PhysicalTileCache::transitionCacheLayout(ChannelCache& cache)
    {
        auto dev = device.getLogicalDevice();

        vk::CommandBufferAllocateInfo allocInfo{};
        allocInfo.commandPool = device.getStagingCommandPool();
        allocInfo.level = vk::CommandBufferLevel::ePrimary;
        allocInfo.commandBufferCount = 1;
        auto cmdBuf = dev.allocateCommandBuffers(allocInfo)[0];

        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        cmdBuf.begin(beginInfo);

        core::ImageUtilities::transitionImageLayout(cmdBuf, cache.image,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor, config.physicalTileCount, 1);

        cmdBuf.end();

        vk::SubmitInfo submitInfo{};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuf;
        device.submitGraphics(submitInfo);
        device.waitGraphicsIdle();
        dev.freeCommandBuffers(device.getStagingCommandPool(), cmdBuf);
    }

    void PhysicalTileCache::destroyChannelCache(ChannelCache& cache)
    {
        auto dev = device.getLogicalDevice();
        if (cache.sampler) dev.destroySampler(cache.sampler);
        if (cache.view) dev.destroyImageView(cache.view);
        if (cache.image) dev.destroyImage(cache.image);
        if (cache.memory) dev.freeMemory(cache.memory);
        cache = {};
    }

    void PhysicalTileCache::createStagingBuffer()
    {
        // Staging buffer large enough for all channels of one tile (5x BC7 compressed)
        // so we can batch all channel uploads into a single command buffer submit
        stagingBufferSize = SVT_TILE_SIZE_BC7 * SVT_CHANNEL_COUNT;

        auto dev = device.getLogicalDevice();
        auto physDev = device.getPhysicalDevice();

        core::BufferInfoRequest bufReq(dev, physDev,
            stagingBufferSize,
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        core::BufferUtilities::createBuffer(bufReq, stagingBuffer, stagingMemory);
        stagingMapped = dev.mapMemory(stagingMemory, 0, stagingBufferSize);
    }

    void PhysicalTileCache::uploadToLayer(ChannelCache& cache, uint32_t layer,
                                          const void* data, uint32_t dataSize)
    {
        if (!data || dataSize == 0 || dataSize > stagingBufferSize) return;

        auto dev = device.getLogicalDevice();

        // Copy data to staging buffer
        std::memcpy(stagingMapped, data, dataSize);

        // Allocate one-time command buffer
        vk::CommandBufferAllocateInfo allocInfo{};
        allocInfo.commandPool = commandPool;
        allocInfo.level = vk::CommandBufferLevel::ePrimary;
        allocInfo.commandBufferCount = 1;
        auto cmdBuf = dev.allocateCommandBuffers(allocInfo)[0];

        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        cmdBuf.begin(beginInfo);

        prepareUploadBarrier(cmdBuf, cache, layer);
        copyBufferToImage(cmdBuf, cache, layer);
        finalizeUploadBarrier(cmdBuf, cache, layer);

        cmdBuf.end();

        vk::SubmitInfo submitInfo{};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuf;
        device.submitGraphics(submitInfo);
        device.waitGraphicsIdle();

        dev.freeCommandBuffers(commandPool, cmdBuf);
    }

    void PhysicalTileCache::prepareUploadBarrier(vk::CommandBuffer cmd, ChannelCache& cache, uint32_t layer)
    {
        vk::ImageMemoryBarrier barrier{};
        barrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = cache.image;
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = layer;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eFragmentShader,
            vk::PipelineStageFlagBits::eTransfer,
            {}, {}, {}, barrier);
    }

    void PhysicalTileCache::copyBufferToImage(vk::CommandBuffer cmd, ChannelCache& cache, uint32_t layer)
    {
        vk::BufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = layer;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = vk::Offset3D{0, 0, 0};
        region.imageExtent = vk::Extent3D{SVT_PHYSICAL_TILE_SIZE, SVT_PHYSICAL_TILE_SIZE, 1};

        cmd.copyBufferToImage(stagingBuffer, cache.image,
                              vk::ImageLayout::eTransferDstOptimal, region);
    }

    void PhysicalTileCache::finalizeUploadBarrier(vk::CommandBuffer cmd, ChannelCache& cache, uint32_t layer)
    {
        vk::ImageMemoryBarrier barrier{};
        barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = cache.image;
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = layer;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader,
            {}, {}, {}, barrier);
    }
}

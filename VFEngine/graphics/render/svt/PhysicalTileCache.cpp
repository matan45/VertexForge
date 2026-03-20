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
        : device_(device)
    {
    }

    PhysicalTileCache::~PhysicalTileCache()
    {
        cleanup();
    }

    void PhysicalTileCache::init(const SVTConfig& config)
    {
        if (initialized_) return;
        config_ = config;

        // Create the three channel caches
        createChannelCache(albedoCache_, vk::Format::eBc7SrgbBlock, "SVT_Albedo");
        createChannelCache(normalCache_, vk::Format::eBc7UnormBlock, "SVT_Normal");
        createChannelCache(ormCache_, vk::Format::eBc7UnormBlock, "SVT_ORM");

        // Initialize tile tracking
        tileSlots_.resize(config_.physicalTileCount);
        freeList_.reserve(config_.physicalTileCount);
        for (uint32_t i = config_.physicalTileCount; i > 0; --i)
        {
            freeList_.push_back(i - 1);
        }

        createStagingBuffer();

        // Create command pool for upload commands
        auto queueFamily = device_.getQueueFamilyIndices().graphicsAndComputeFamily.value();
        vk::CommandPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer
                       | vk::CommandPoolCreateFlagBits::eTransient;
        poolInfo.queueFamilyIndex = queueFamily;
        commandPool_ = device_.getLogicalDevice().createCommandPool(poolInfo);

        initialized_ = true;
        vfLogInfo("SVT PhysicalTileCache initialized: {} tiles per channel, {}x{} physical tile size",
                     config_.physicalTileCount, SVT_PHYSICAL_TILE_SIZE, SVT_PHYSICAL_TILE_SIZE);
    }

    void PhysicalTileCache::cleanup()
    {
        if (!initialized_) return;

        auto dev = device_.getLogicalDevice();
        dev.waitIdle();

        if (commandPool_)
        {
            dev.destroyCommandPool(commandPool_);
            commandPool_ = nullptr;
        }

        if (stagingBuffer_)
        {
            if (stagingMapped_)
            {
                dev.unmapMemory(stagingMemory_);
                stagingMapped_ = nullptr;
            }
            core::BufferUtilities::destroyBuffer(dev, stagingBuffer_, stagingMemory_);
        }

        destroyChannelCache(albedoCache_);
        destroyChannelCache(normalCache_);
        destroyChannelCache(ormCache_);

        tileSlots_.clear();
        freeList_.clear();
        initialized_ = false;
    }

    uint32_t PhysicalTileCache::allocateTile()
    {
        if (freeList_.empty()) return SVT_INVALID_TILE;

        uint32_t idx = freeList_.back();
        freeList_.pop_back();
        tileSlots_[idx].occupied = true;
        return idx;
    }

    void PhysicalTileCache::freeTile(uint32_t tileIndex)
    {
        if (tileIndex >= config_.physicalTileCount) return;
        tileSlots_[tileIndex] = {};
        freeList_.push_back(tileIndex);
    }

    uint32_t PhysicalTileCache::evictLRU(uint64_t currentFrame)
    {
        uint32_t bestIdx = SVT_INVALID_TILE;
        uint64_t oldestFrame = currentFrame;

        for (uint32_t i = 0; i < config_.physicalTileCount; ++i)
        {
            auto& slot = tileSlots_[i];
            if (slot.occupied && slot.lastUsedFrame < oldestFrame)
            {
                oldestFrame = slot.lastUsedFrame;
                bestIdx = i;
            }
        }

        if (bestIdx != SVT_INVALID_TILE)
        {
            tileSlots_[bestIdx] = {};
            // Don't push to freeList — caller will reuse immediately
        }

        return bestIdx;
    }

    void PhysicalTileCache::touchTile(uint32_t tileIndex, uint64_t frame)
    {
        if (tileIndex < config_.physicalTileCount)
        {
            tileSlots_[tileIndex].lastUsedFrame = frame;
        }
    }

    void PhysicalTileCache::setTileMapping(uint32_t tileIndex, const VirtualTileCoord& virtualCoord)
    {
        if (tileIndex < config_.physicalTileCount)
        {
            tileSlots_[tileIndex].virtualCoord = virtualCoord;
        }
    }

    void PhysicalTileCache::uploadTileData(uint32_t tileIndex, uint32_t channelIndex,
                                           const void* compressedData, uint32_t dataSize)
    {
        if (tileIndex >= config_.physicalTileCount || !compressedData) return;

        switch (channelIndex)
        {
        case 0: uploadToLayer(albedoCache_, tileIndex, compressedData, dataSize); break;
        case 1: uploadToLayer(normalCache_, tileIndex, compressedData, dataSize); break;
        case 2: uploadToLayer(ormCache_, tileIndex, compressedData, dataSize); break;
        default: break;
        }
    }

    void PhysicalTileCache::flushUploads()
    {
        // Uploads are submitted immediately in uploadToLayer via one-time command buffers.
        // This is a sync point if needed in the future for batched uploads.
    }

    // ---- Private ----

    void PhysicalTileCache::createChannelCache(ChannelCache& cache, vk::Format format, const char* debugName)
    {
        auto dev = device_.getLogicalDevice();
        auto physDev = device_.getPhysicalDevice();

        cache.format = format;

        // Create 2D array image: each layer is one physical tile
        core::ImageInfoRequest imgReq(dev, physDev,
            SVT_PHYSICAL_TILE_SIZE,
            SVT_PHYSICAL_TILE_SIZE,
            config_.physicalTileCount,  // array layers = tile count
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
            config_.physicalTileCount,
            1
        );
        core::ImageUtilities::createImageView(viewReq, cache.view);

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

        // Transition image to shader-read-optimal
        vk::CommandBufferAllocateInfo allocInfo{};
        allocInfo.commandPool = device_.getStagingCommandPool();
        allocInfo.level = vk::CommandBufferLevel::ePrimary;
        allocInfo.commandBufferCount = 1;
        auto cmdBuf = dev.allocateCommandBuffers(allocInfo)[0];

        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        cmdBuf.begin(beginInfo);

        core::ImageUtilities::transitionImageLayout(cmdBuf, cache.image,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor, config_.physicalTileCount, 1);

        cmdBuf.end();

        vk::SubmitInfo submitInfo{};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuf;
        device_.getGraphicsQueue().submit(submitInfo);
        device_.getGraphicsQueue().waitIdle();
        dev.freeCommandBuffers(device_.getStagingCommandPool(), cmdBuf);

        vfLogInfo("SVT: Created {} cache: {}x{}x{} layers, format {}",
                      debugName, SVT_PHYSICAL_TILE_SIZE, SVT_PHYSICAL_TILE_SIZE,
                      config_.physicalTileCount, static_cast<int>(format));
    }

    void PhysicalTileCache::destroyChannelCache(ChannelCache& cache)
    {
        auto dev = device_.getLogicalDevice();
        if (cache.sampler) dev.destroySampler(cache.sampler);
        if (cache.view) dev.destroyImageView(cache.view);
        if (cache.image) dev.destroyImage(cache.image);
        if (cache.memory) dev.freeMemory(cache.memory);
        cache = {};
    }

    void PhysicalTileCache::createStagingBuffer()
    {
        // Staging buffer large enough for one tile (BC7 compressed)
        stagingBufferSize_ = SVT_TILE_SIZE_BC7;

        auto dev = device_.getLogicalDevice();
        auto physDev = device_.getPhysicalDevice();

        core::BufferInfoRequest bufReq(dev, physDev,
            stagingBufferSize_,
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        core::BufferUtilities::createBuffer(bufReq, stagingBuffer_, stagingMemory_);
        stagingMapped_ = dev.mapMemory(stagingMemory_, 0, stagingBufferSize_);
    }

    void PhysicalTileCache::uploadToLayer(ChannelCache& cache, uint32_t layer,
                                          const void* data, uint32_t dataSize)
    {
        if (!data || dataSize == 0 || dataSize > stagingBufferSize_) return;

        auto dev = device_.getLogicalDevice();

        // Copy data to staging buffer
        std::memcpy(stagingMapped_, data, dataSize);

        // Allocate one-time command buffer
        vk::CommandBufferAllocateInfo allocInfo{};
        allocInfo.commandPool = commandPool_;
        allocInfo.level = vk::CommandBufferLevel::ePrimary;
        allocInfo.commandBufferCount = 1;
        auto cmdBuf = dev.allocateCommandBuffers(allocInfo)[0];

        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        cmdBuf.begin(beginInfo);

        // Transition layer to transfer dst
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

        cmdBuf.pipelineBarrier(
            vk::PipelineStageFlagBits::eFragmentShader,
            vk::PipelineStageFlagBits::eTransfer,
            {}, {}, {}, barrier);

        // Copy staging buffer to image layer
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

        cmdBuf.copyBufferToImage(stagingBuffer_, cache.image,
                                 vk::ImageLayout::eTransferDstOptimal, region);

        // Transition back to shader read
        barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        cmdBuf.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader,
            {}, {}, {}, barrier);

        cmdBuf.end();

        vk::SubmitInfo submitInfo{};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuf;
        device_.getGraphicsQueue().submit(submitInfo);
        device_.getGraphicsQueue().waitIdle();

        dev.freeCommandBuffers(commandPool_, cmdBuf);
    }
}

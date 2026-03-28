#include "TextureStreamManager.hpp"
#include "BindlessTextureManager.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/BufferUtilities.hpp"
#include "../../../core/ImageUtilities.hpp"
#include "../../../core/DeferredDeletionQueue.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    vk::Format resolveVulkanFormat(resource::TextureCompressionFormat compression, vk::Format uncompressedFormat)
    {
        switch (compression)
        {
            case resource::TextureCompressionFormat::BC7:
            {
                if (uncompressedFormat == vk::Format::eR8G8B8A8Srgb ||
                    uncompressedFormat == vk::Format::eB8G8R8A8Srgb)
                    return vk::Format::eBc7SrgbBlock;
                return vk::Format::eBc7UnormBlock;
            }
            case resource::TextureCompressionFormat::BC6H:
                return vk::Format::eBc6HUfloatBlock;
            default:
                return uncompressedFormat;
        }
    }
}

namespace render::gpudriven
{
    TextureStreamManager::TextureStreamManager(core::Device& device, BindlessTextureManager& bindlessMgr)
        : device(device), bindlessTextures(bindlessMgr)
    {
    }

    TextureStreamManager::~TextureStreamManager()
    {
        cleanup();
    }

    void TextureStreamManager::init()
    {
        vk::CommandPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer |
                         vk::CommandPoolCreateFlagBits::eTransient;
        poolInfo.queueFamilyIndex = device.getQueueFamilyIndices().graphicsAndComputeFamily.value();
        commandPool = device.getLogicalDevice().createCommandPool(poolInfo);

        constexpr size_t initialStagingSize = 32 * 1024 * 1024;
        createStagingBuffer(std::max(initialStagingSize, config.maxBytesPerFrame));

        vfLogInfo("TextureStreamManager: Initialized (VRAM budget: {} MB, max {}/frame)",
                  config.vramBudgetBytes / (1024 * 1024), config.maxBytesPerFrame / (1024 * 1024));
    }

    void TextureStreamManager::cleanup()
    {
        if (!commandPool && textures.empty() && !stagingBuffer) return;

        vk::Device vkDevice = device.getLogicalDevice();
        if (!vkDevice) return;
        vkDevice.waitIdle();

        for (auto& future : pendingReads)
        {
            if (future.valid()) future.wait();
        }
        pendingReads.clear();
        uploadQueue.clear();
        inFlightReads.clear();

        for (auto& [path, tex] : textures)
        {
            if (tex.currentSampler) vkDevice.destroySampler(tex.currentSampler);
            if (tex.view) vkDevice.destroyImageView(tex.view);
            if (tex.image) vkDevice.destroyImage(tex.image);
            if (tex.allocation) { device.getMemoryManager().free(tex.allocation); tex.allocation = {}; }
        }
        textures.clear();
        streamHandles.clear();

        if (stagingBuffer)
        {
            vkDevice.destroyBuffer(stagingBuffer);
            stagingBuffer = nullptr;
            device.getMemoryManager().free(stagingAllocation);
            stagingAllocation = {};
            stagingMapped = nullptr;
        }

        if (commandPool)
        {
            vkDevice.destroyCommandPool(commandPool);
            commandPool = nullptr;
        }

        currentVRAMUsage = 0;
    }

    void TextureStreamManager::createStagingBuffer(size_t size)
    {
        if (stagingBuffer)
        {
            device.getLogicalDevice().destroyBuffer(stagingBuffer);
            device.getMemoryManager().free(stagingAllocation);
            stagingAllocation = {};
        }

        stagingBufferSize = size;

        core::BufferInfoRequest bufInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        bufInfo.size = static_cast<vk::DeviceSize>(size);
        bufInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
        bufInfo.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                             vk::MemoryPropertyFlagBits::eHostCoherent;

        core::BufferUtilities::createBuffer(bufInfo, stagingBuffer, stagingAllocation, device.getMemoryManager());

        stagingMapped = stagingAllocation.mappedPtr;
    }

    uint32_t TextureStreamManager::registerTexture(const std::string& path, vk::Format format)
    {
        auto it = textures.find(path);
        if (it != textures.end())
            return it->second.bindlessIndex;

        auto handle = resource::TextureStreamResource::openStream(path);
        if (!handle)
        {
            vfLogWarning("TextureStreamManager: Failed to open stream for '{}'", path);
            return INVALID_TEXTURE_INDEX;
        }

        const auto& header = handle->getHeader();
        format = resolveVulkanFormat(header.compression, format);

        uint32_t tailStart = (header.mipLevels > config.tailMipCount)
                                 ? header.mipLevels - config.tailMipCount
                                 : 0;

        std::vector<resource::MipLevelData> tailMips;
        if (!handle->readMipRange(tailStart, header.mipLevels - 1, tailMips))
        {
            vfLogWarning("TextureStreamManager: Failed to read tail mips for '{}'", path);
            return INVALID_TEXTURE_INDEX;
        }

        StreamableTexture tex;
        tex.path = path;
        tex.format = format;
        tex.width = header.width;
        tex.height = header.height;
        tex.totalMipLevels = header.mipLevels;
        tex.lowestLoadedMip = tailStart;

        if (!createStreamableImage(tex, header, tailMips))
        {
            vfLogWarning("TextureStreamManager: Failed to create streamable image for '{}'", path);
            return INVALID_TEXTURE_INDEX;
        }

        tex.bindlessIndex = bindlessTextures.registerTexture(path, tex.view, tex.currentSampler);
        if (tex.bindlessIndex == INVALID_TEXTURE_INDEX)
        {
            vk::Device vkDevice = device.getLogicalDevice();
            vkDevice.destroySampler(tex.currentSampler);
            vkDevice.destroyImageView(tex.view);
            vkDevice.destroyImage(tex.image);
            device.getMemoryManager().free(tex.allocation); tex.allocation = {};
            return INVALID_TEXTURE_INDEX;
        }

        streamHandles[path] = std::move(handle);
        textures[path] = std::move(tex);

        return textures[path].bindlessIndex;
    }

    vk::Sampler TextureStreamManager::createMipClampedSampler(uint32_t minLod, uint32_t maxLod)
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = device.getPhysicalDevice().getProperties().limits.maxSamplerAnisotropy;
        samplerInfo.minLod = static_cast<float>(minLod);
        samplerInfo.maxLod = static_cast<float>(maxLod);

        return device.getLogicalDevice().createSampler(samplerInfo);
    }

    void TextureStreamManager::updateSamplerAndDescriptor(StreamableTexture& tex)
    {
        vk::Sampler oldSampler = tex.currentSampler;
        tex.currentSampler = createMipClampedSampler(tex.lowestLoadedMip, tex.totalMipLevels - 1);
        bindlessTextures.updateDescriptor(tex.bindlessIndex, tex.view, tex.currentSampler);

        if (deletionQueue && oldSampler)
            deletionQueue->queueSampler(oldSampler);
        else if (oldSampler)
            device.getLogicalDevice().destroySampler(oldSampler);
    }

    uint32_t TextureStreamManager::getTextureIndex(const std::string& path) const
    {
        auto it = textures.find(path);
        if (it != textures.end())
            return it->second.bindlessIndex;
        return INVALID_TEXTURE_INDEX;
    }

    bool TextureStreamManager::isRegistered(const std::string& path) const
    {
        return textures.contains(path);
    }

    void TextureStreamManager::resetDistances()
    {
        for (auto& [path, tex] : textures)
            tex.distanceToCamera = std::numeric_limits<float>::max();
    }

    void TextureStreamManager::updateTextureDistance(const std::string& path, float distance)
    {
        auto it = textures.find(path);
        if (it != textures.end())
            it->second.distanceToCamera = std::min(it->second.distanceToCamera, distance);
    }

    uint32_t TextureStreamManager::calculateDesiredMip(float distance, uint32_t totalMips) const
    {
        if (distance <= 0.0f || totalMips <= 1) return 0;

        constexpr float texelDensityFactor = 0.01f;
        float desiredMipFloat = std::floor(std::log2(distance * texelDensityFactor + 1.0f));
        uint32_t desiredMip = static_cast<uint32_t>(std::max(0.0f, desiredMipFloat));

        return std::min(desiredMip, totalMips - 1);
    }

    float TextureStreamManager::calculatePriority(uint32_t currentMip, uint32_t desiredMip, float distance) const
    {
        float qualityGap = static_cast<float>(currentMip - desiredMip);
        return qualityGap * 10.0f + 1.0f / (distance + 1.0f);
    }

    size_t TextureStreamManager::estimateMipVRAM(uint32_t width, uint32_t height, uint32_t mipLevel,
                                                  vk::Format format) const
    {
        uint32_t mipW = std::max(1u, width >> mipLevel);
        uint32_t mipH = std::max(1u, height >> mipLevel);

        if (format == vk::Format::eBc7UnormBlock || format == vk::Format::eBc7SrgbBlock)
        {
            uint32_t blocksW = (mipW + 3) / 4;
            uint32_t blocksH = (mipH + 3) / 4;
            return blocksW * blocksH * 16;
        }

        return mipW * mipH * 4;
    }

    size_t TextureStreamManager::estimateFullImageVRAM(uint32_t width, uint32_t height,
                                                        uint32_t mipLevels, vk::Format format) const
    {
        size_t total = 0;
        for (uint32_t i = 0; i < mipLevels; ++i)
            total += estimateMipVRAM(width, height, i, format);
        return total;
    }

    void TextureStreamManager::clear()
    {
        cleanup();
    }
}

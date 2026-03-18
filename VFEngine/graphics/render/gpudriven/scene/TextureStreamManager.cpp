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
        // Create command pool for upload commands
        vk::CommandPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer |
                         vk::CommandPoolCreateFlagBits::eTransient;
        poolInfo.queueFamilyIndex = device.getQueueFamilyIndices().graphicsAndComputeFamily.value();
        commandPool = device.getLogicalDevice().createCommandPool(poolInfo);

        // 32MB initial staging — covers up to 2048x2048 RGBA uncompressed (16MB) with headroom.
        // Grows automatically if a larger mip is encountered.
        constexpr size_t initialStagingSize = 32 * 1024 * 1024;
        createStagingBuffer(std::max(initialStagingSize, config.maxBytesPerFrame));

        vfLogInfo("TextureStreamManager: Initialized (VRAM budget: {} MB, max {}/frame)",
                  config.vramBudgetBytes / (1024 * 1024), config.maxBytesPerFrame / (1024 * 1024));
    }

    void TextureStreamManager::cleanup()
    {
        if (!commandPool && textures.empty() && !stagingBuffer)
        {
            return; // Already cleaned up
        }

        vk::Device vkDevice = device.getLogicalDevice();
        if (!vkDevice)
        {
            return;
        }
        vkDevice.waitIdle();

        // Wait for all pending reads
        for (auto& future : pendingReads)
        {
            if (future.valid())
            {
                future.wait();
            }
        }
        pendingReads.clear();
        uploadQueue.clear();
        inFlightReads.clear();

        // Destroy all streamable textures
        for (auto& [path, tex] : textures)
        {
            if (tex.currentSampler) vkDevice.destroySampler(tex.currentSampler);
            if (tex.view) vkDevice.destroyImageView(tex.view);
            if (tex.image) vkDevice.destroyImage(tex.image);
            if (tex.memory) vkDevice.freeMemory(tex.memory);
        }
        textures.clear();
        streamHandles.clear();

        // Destroy staging buffer
        if (stagingBuffer)
        {
            vkDevice.unmapMemory(stagingMemory);
            vkDevice.destroyBuffer(stagingBuffer);
            vkDevice.freeMemory(stagingMemory);
            stagingBuffer = nullptr;
            stagingMemory = nullptr;
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
            device.getLogicalDevice().unmapMemory(stagingMemory);
            device.getLogicalDevice().destroyBuffer(stagingBuffer);
            device.getLogicalDevice().freeMemory(stagingMemory);
        }

        stagingBufferSize = size;

        core::BufferInfoRequest bufInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        bufInfo.size = static_cast<vk::DeviceSize>(size);
        bufInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
        bufInfo.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                             vk::MemoryPropertyFlagBits::eHostCoherent;

        core::BufferUtilities::createBuffer(bufInfo, stagingBuffer, stagingMemory);

        [[maybe_unused]] auto result = device.getLogicalDevice().mapMemory(
            stagingMemory, 0, static_cast<vk::DeviceSize>(size), {}, &stagingMapped);
    }

    uint32_t TextureStreamManager::registerTexture(const std::string& path, vk::Format format)
    {
        // Already registered?
        auto it = textures.find(path);
        if (it != textures.end())
        {
            return it->second.bindlessIndex;
        }

        // Open stream handle for file I/O
        auto handle = resource::TextureStreamResource::openStream(path);
        if (!handle)
        {
            vfLogWarning("TextureStreamManager: Failed to open stream for '{}'", path);
            return INVALID_TEXTURE_INDEX;
        }

        const auto& header = handle->getHeader();

        // Resolve Vulkan format based on file compression (BC7, BC6H, etc.)
        format = resolveVulkanFormat(header.compression, format);

        // Read tail mips (last N mips) immediately
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

        // Register in bindless texture manager
        tex.bindlessIndex = bindlessTextures.registerTexture(path, tex.view, tex.currentSampler);
        if (tex.bindlessIndex == INVALID_TEXTURE_INDEX)
        {
            // Cleanup
            vk::Device vkDevice = device.getLogicalDevice();
            vkDevice.destroySampler(tex.currentSampler);
            vkDevice.destroyImageView(tex.view);
            vkDevice.destroyImage(tex.image);
            vkDevice.freeMemory(tex.memory);
            return INVALID_TEXTURE_INDEX;
        }

        // Store handle and texture
        streamHandles[path] = std::move(handle);
        textures[path] = std::move(tex);

        return textures[path].bindlessIndex;
    }

    bool TextureStreamManager::createStreamableImage(
        StreamableTexture& tex,
        const resource::TextureStreamHeader& header,
        const std::vector<resource::MipLevelData>& tailMips)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Create image with full mip chain
        core::ImageInfoRequest imageInfo(
            vkDevice,
            device.getPhysicalDevice(),
            header.width, header.height, 1, header.mipLevels,
            tex.format,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::ImageUtilities::createImage(imageInfo, tex.image, tex.memory);

        // Estimate VRAM usage
        tex.gpuMemoryUsage = estimateFullImageVRAM(header.width, header.height, header.mipLevels, tex.format);
        currentVRAMUsage += tex.gpuMemoryUsage;

        // Create image view spanning all mip levels
        core::ImageViewInfoRequest viewInfo(
            vkDevice, tex.image, tex.format,
            vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2D, 1, header.mipLevels
        );
        core::ImageUtilities::createImageView(viewInfo, tex.view);

        // Upload tail mips via staging buffer
        // Allocate a one-time command buffer
        vk::CommandBufferAllocateInfo cmdAllocInfo{};
        cmdAllocInfo.level = vk::CommandBufferLevel::ePrimary;
        cmdAllocInfo.commandPool = commandPool;
        cmdAllocInfo.commandBufferCount = 1;
        auto cmdBuffers = vkDevice.allocateCommandBuffers(cmdAllocInfo);
        vk::CommandBuffer cmd = cmdBuffers[0];

        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        cmd.begin(beginInfo);

        // Transition ALL mip levels to transfer dst
        vk::ImageMemoryBarrier barrier{};
        barrier.oldLayout = vk::ImageLayout::eUndefined;
        barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = tex.image;
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = header.mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                            vk::PipelineStageFlagBits::eTransfer,
                            {}, nullptr, nullptr, barrier);

        // Calculate total tail mip data size and grow staging buffer if needed
        size_t totalTailSize = 0;
        for (const auto& mip : tailMips)
        {
            totalTailSize += mip.dataSize;
        }
        if (totalTailSize > stagingBufferSize)
        {
            createStagingBuffer(totalTailSize * 2);
        }

        // Copy tail mips from staging
        size_t stagingOffset = 0;
        uint32_t tailStart = tex.lowestLoadedMip;

        for (uint32_t i = 0; i < static_cast<uint32_t>(tailMips.size()); ++i)
        {
            const auto& mip = tailMips[i];
            uint32_t mipLevel = tailStart + i;

            memcpy(static_cast<char*>(stagingMapped) + stagingOffset, mip.data.data(), mip.dataSize);

            vk::BufferImageCopy copyRegion{};
            copyRegion.bufferOffset = static_cast<vk::DeviceSize>(stagingOffset);
            copyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            copyRegion.imageSubresource.mipLevel = mipLevel;
            copyRegion.imageSubresource.baseArrayLayer = 0;
            copyRegion.imageSubresource.layerCount = 1;
            copyRegion.imageExtent = vk::Extent3D{mip.width, mip.height, 1};

            cmd.copyBufferToImage(stagingBuffer, tex.image,
                                  vk::ImageLayout::eTransferDstOptimal, copyRegion);

            stagingOffset += mip.dataSize;
        }

        // Transition ALL mip levels to shader read optimal
        barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                            vk::PipelineStageFlagBits::eFragmentShader,
                            {}, nullptr, nullptr, barrier);

        cmd.end();

        vk::SubmitInfo submitInfo{};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        device.getGraphicsQueue().submit(submitInfo);
        device.getGraphicsQueue().waitIdle();

        vkDevice.freeCommandBuffers(commandPool, cmd);

        // Create sampler with minLod = lowestLoadedMip
        tex.currentSampler = createMipClampedSampler(tex.lowestLoadedMip, header.mipLevels - 1);

        return true;
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

    void TextureStreamManager::update(const glm::vec3& cameraPos, uint64_t frameIndex)
    {
        currentFrame = frameIndex;
        stats = {};
        stats.vramBudgetBytes = config.vramBudgetBytes;
        stats.vramUsedBytes = currentVRAMUsage;

        processCompletedReads();
        processUploads(cameraPos);
        updatePriorities(cameraPos);
        submitReadRequests();
        processEvictions();

        // Update stats
        stats.totalRegistered = static_cast<uint32_t>(textures.size());
        stats.pendingReads = static_cast<uint32_t>(pendingReads.size());
        stats.vramUsedBytes = currentVRAMUsage;

        uint32_t fullyLoaded = 0;
        uint32_t partiallyLoaded = 0;
        for (const auto& [path, tex] : textures)
        {
            if (tex.lowestLoadedMip == 0)
                fullyLoaded++;
            else
                partiallyLoaded++;
        }
        stats.fullyLoaded = fullyLoaded;
        stats.partiallyLoaded = partiallyLoaded;
    }

    uint32_t TextureStreamManager::getTextureIndex(const std::string& path) const
    {
        auto it = textures.find(path);
        if (it != textures.end())
        {
            return it->second.bindlessIndex;
        }
        return INVALID_TEXTURE_INDEX;
    }

    bool TextureStreamManager::isRegistered(const std::string& path) const
    {
        return textures.contains(path);
    }

    void TextureStreamManager::processCompletedReads()
    {
        auto it = pendingReads.begin();
        while (it != pendingReads.end())
        {
            if (it->wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
            {
                auto result = it->get();
                inFlightReads.erase({result.path, result.mipLevel});
                if (result.success)
                {
                    std::lock_guard lock(uploadQueueMutex);
                    uploadQueue.push_back(std::move(result));
                }
                it = pendingReads.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void TextureStreamManager::processUploads(const glm::vec3& cameraPos)
    {
        std::vector<TextureMipReadResult> toUpload;
        {
            std::lock_guard lock(uploadQueueMutex);
            toUpload = std::move(uploadQueue);
            uploadQueue.clear();
        }

        // Collect valid uploads first, then batch into a single command buffer
        struct PendingUpload
        {
            std::string path;
            uint32_t mipLevel;
            resource::MipLevelData mipData;
        };
        std::vector<PendingUpload> validUploads;
        std::vector<TextureMipReadResult> excess;

        uint32_t uploads = 0;
        size_t bytesUploaded = 0;

        for (auto& result : toUpload)
        {
            if (uploads >= config.maxUploadsPerFrame || bytesUploaded >= config.maxBytesPerFrame)
            {
                excess.push_back(std::move(result));
                continue;
            }

            auto texIt = textures.find(result.path);
            if (texIt == textures.end()) continue;

            auto& tex = texIt->second;
            if (result.mipLevel >= tex.lowestLoadedMip) continue;

            bytesUploaded += result.mipData.dataSize;
            uploads++;
            validUploads.push_back({result.path, result.mipLevel, std::move(result.mipData)});
        }

        // Re-queue excess in a single lock
        if (!excess.empty())
        {
            std::lock_guard lock(uploadQueueMutex);
            for (auto& e : excess)
            {
                uploadQueue.push_back(std::move(e));
            }
        }

        if (!validUploads.empty())
        {
            // Ensure staging buffer can hold the largest single mip
            size_t maxMipSize = 0;
            size_t totalSize = 0;
            for (const auto& up : validUploads)
            {
                maxMipSize = std::max(maxMipSize, static_cast<size_t>(up.mipData.dataSize));
                totalSize += up.mipData.dataSize;
            }
            if (totalSize > stagingBufferSize)
            {
                createStagingBuffer(totalSize * 2);
            }

            vk::Device vkDevice = device.getLogicalDevice();

            // Record all uploads into a single command buffer
            vk::CommandBufferAllocateInfo cmdAllocInfo{};
            cmdAllocInfo.level = vk::CommandBufferLevel::ePrimary;
            cmdAllocInfo.commandPool = commandPool;
            cmdAllocInfo.commandBufferCount = 1;
            auto cmdBuffers = vkDevice.allocateCommandBuffers(cmdAllocInfo);
            vk::CommandBuffer cmd = cmdBuffers[0];

            vk::CommandBufferBeginInfo beginInfo{};
            beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            cmd.begin(beginInfo);

            // Copy all mip data into staging buffer at sequential offsets
            size_t stagingOffset = 0;
            for (const auto& up : validUploads)
            {
                memcpy(static_cast<char*>(stagingMapped) + stagingOffset,
                       up.mipData.data.data(), up.mipData.dataSize);

                auto texIt = textures.find(up.path);
                auto& tex = texIt->second;

                // Transition mip to transfer dst
                vk::ImageMemoryBarrier barrier{};
                barrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = tex.image;
                barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
                barrier.subresourceRange.baseMipLevel = up.mipLevel;
                barrier.subresourceRange.levelCount = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = 1;
                barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
                barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

                cmd.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader,
                                    vk::PipelineStageFlagBits::eTransfer,
                                    {}, nullptr, nullptr, barrier);

                vk::BufferImageCopy copyRegion{};
                copyRegion.bufferOffset = static_cast<vk::DeviceSize>(stagingOffset);
                copyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
                copyRegion.imageSubresource.mipLevel = up.mipLevel;
                copyRegion.imageSubresource.baseArrayLayer = 0;
                copyRegion.imageSubresource.layerCount = 1;
                copyRegion.imageExtent = vk::Extent3D{up.mipData.width, up.mipData.height, 1};

                cmd.copyBufferToImage(stagingBuffer, tex.image,
                                      vk::ImageLayout::eTransferDstOptimal, copyRegion);

                // Transition back to shader read
                barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
                barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
                barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

                cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                    vk::PipelineStageFlagBits::eFragmentShader,
                                    {}, nullptr, nullptr, barrier);

                stagingOffset += up.mipData.dataSize;
            }

            cmd.end();

            // Single submit + single waitIdle for all uploads this frame
            vk::SubmitInfo submitInfo{};
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &cmd;
            device.getGraphicsQueue().submit(submitInfo);
            device.getGraphicsQueue().waitIdle();

            vkDevice.freeCommandBuffers(commandPool, cmd);

            // Update sampler/descriptors after all uploads complete
            for (const auto& up : validUploads)
            {
                auto texIt = textures.find(up.path);
                auto& tex = texIt->second;
                tex.lowestLoadedMip = up.mipLevel;
                updateSamplerAndDescriptor(tex);
            }
        }

        stats.uploadsThisFrame = uploads;
        stats.bytesUploadedThisFrame = bytesUploaded;
    }

    void TextureStreamManager::updateSamplerAndDescriptor(StreamableTexture& tex)
    {
        vk::Sampler oldSampler = tex.currentSampler;

        // Create new sampler with updated minLod
        tex.currentSampler = createMipClampedSampler(tex.lowestLoadedMip, tex.totalMipLevels - 1);

        // Update the bindless descriptor
        bindlessTextures.updateDescriptor(tex.bindlessIndex, tex.view, tex.currentSampler);

        // Queue old sampler for deferred deletion
        if (deletionQueue && oldSampler)
        {
            deletionQueue->queueSampler(oldSampler);
        }
        else if (oldSampler)
        {
            device.getLogicalDevice().destroySampler(oldSampler);
        }
    }

    void TextureStreamManager::resetDistances()
    {
        for (auto& [path, tex] : textures)
        {
            tex.distanceToCamera = std::numeric_limits<float>::max();
        }
    }

    void TextureStreamManager::updateTextureDistance(const std::string& path, float distance)
    {
        auto it = textures.find(path);
        if (it != textures.end())
        {
            it->second.distanceToCamera = std::min(it->second.distanceToCamera, distance);
        }
    }

    void TextureStreamManager::updatePriorities(const glm::vec3& cameraPos)
    {
        for (auto& [path, tex] : textures)
        {
            tex.lastAccessFrame = currentFrame;

            // If no object updated this texture's distance this frame,
            // it's not visible — set a large distance so it becomes an eviction candidate.
            if (tex.distanceToCamera >= std::numeric_limits<float>::max())
            {
                tex.distanceToCamera = 10000.0f;
            }
        }
    }

    void TextureStreamManager::submitReadRequests()
    {
        // Build priority queue of textures that need higher mips
        std::priority_queue<TextureStreamPriorityEntry> priorityQueue;

        for (const auto& [path, tex] : textures)
        {
            uint32_t desiredMip = calculateDesiredMip(tex.distanceToCamera, tex.totalMipLevels);

            if (desiredMip < tex.lowestLoadedMip)
            {
                float priority = calculatePriority(tex.lowestLoadedMip, desiredMip, tex.distanceToCamera);
                priorityQueue.push({path, desiredMip, priority});
            }
        }

        // Submit async reads for top priority textures (limited per frame)
        uint32_t readsSubmitted = 0;
        constexpr uint32_t maxReadsPerFrame = 4;

        while (!priorityQueue.empty() && readsSubmitted < maxReadsPerFrame)
        {
            auto entry = priorityQueue.top();
            priorityQueue.pop();

            auto texIt = textures.find(entry.path);
            if (texIt == textures.end()) continue;

            auto& tex = texIt->second;

            // Read the next mip level down (one at a time)
            uint32_t nextMip = tex.lowestLoadedMip - 1;

            auto handleIt = streamHandles.find(entry.path);
            if (handleIt == streamHandles.end()) continue;

            // Skip if this (path, mipLevel) is already in-flight
            auto readKey = std::make_pair(entry.path, nextMip);
            if (inFlightReads.contains(readKey))
            {
                continue;
            }

            auto* handlePtr = handleIt->second.get();
            std::string pathCopy = entry.path;

            inFlightReads.insert(readKey);

            pendingReads.push_back(std::async(std::launch::async,
                [handlePtr, nextMip, pathCopy]() -> TextureMipReadResult
                {
                    TextureMipReadResult result;
                    result.path = pathCopy;
                    result.mipLevel = nextMip;
                    result.success = handlePtr->readMipLevel(nextMip, result.mipData);
                    return result;
                }));

            readsSubmitted++;
        }
    }

    void TextureStreamManager::processEvictions()
    {
        float usageRatio = static_cast<float>(currentVRAMUsage) / static_cast<float>(config.vramBudgetBytes);
        if (usageRatio < config.evictionThreshold)
        {
            return;
        }

        // Gather eviction candidates (textures with high-res mips loaded and far from camera)
        struct EvictionCandidate
        {
            std::string path;
            float score; // higher = better candidate for eviction
        };

        std::vector<EvictionCandidate> candidates;
        for (const auto& [path, tex] : textures)
        {
            // Only evict if there are mips above tail to remove
            if (tex.lowestLoadedMip < tex.totalMipLevels - config.tailMipCount)
            {
                float ageFactor = static_cast<float>(currentFrame - tex.lastAccessFrame + 1);
                float score = tex.distanceToCamera * ageFactor;
                candidates.push_back({path, score});
            }
        }

        // Sort by score (highest = best eviction candidate)
        std::sort(candidates.begin(), candidates.end(),
                  [](const auto& a, const auto& b) { return a.score > b.score; });

        uint32_t evictions = 0;
        for (const auto& candidate : candidates)
        {
            if (static_cast<float>(currentVRAMUsage) / static_cast<float>(config.vramBudgetBytes) <= config.evictionTarget)
            {
                break;
            }

            auto texIt = textures.find(candidate.path);
            if (texIt == textures.end()) continue;

            auto& tex = texIt->second;

            // "Evict" by raising minLod (sampler trick). VRAM image stays allocated,
            // but we account for the evicted mips in our budget tracking so the
            // guard loop converges. The evicted mips are logically stale and will
            // need re-streaming if the object moves close again.
            uint32_t newLowestMip = tex.totalMipLevels - config.tailMipCount;
            if (newLowestMip > tex.lowestLoadedMip)
            {
                // Estimate VRAM freed by the evicted mip levels
                size_t freedBytes = 0;
                for (uint32_t m = tex.lowestLoadedMip; m < newLowestMip; ++m)
                {
                    freedBytes += estimateMipVRAM(tex.width, tex.height, m, tex.format);
                }
                if (freedBytes <= currentVRAMUsage)
                {
                    currentVRAMUsage -= freedBytes;
                }
                else
                {
                    currentVRAMUsage = 0;
                }

                tex.lowestLoadedMip = newLowestMip;
                updateSamplerAndDescriptor(tex);
                evictions++;
            }
        }

        stats.evictionsThisFrame = evictions;
    }

    uint32_t TextureStreamManager::calculateDesiredMip(float distance, uint32_t totalMips) const
    {
        if (distance <= 0.0f || totalMips <= 1)
        {
            return 0;
        }

        // texelDensityFactor: at distance 1.0 we want mip 0, at distance 2.0 mip 1, etc.
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

        size_t bytesPerPixel = 4; // Default for RGBA8
        if (format == vk::Format::eBc7UnormBlock || format == vk::Format::eBc7SrgbBlock)
        {
            // BC7: 1 byte per pixel (16 bytes per 4x4 block)
            uint32_t blocksW = (mipW + 3) / 4;
            uint32_t blocksH = (mipH + 3) / 4;
            return blocksW * blocksH * 16;
        }

        return mipW * mipH * bytesPerPixel;
    }

    size_t TextureStreamManager::estimateFullImageVRAM(uint32_t width, uint32_t height,
                                                        uint32_t mipLevels, vk::Format format) const
    {
        size_t total = 0;
        for (uint32_t i = 0; i < mipLevels; ++i)
        {
            total += estimateMipVRAM(width, height, i, format);
        }
        return total;
    }

    void TextureStreamManager::clear()
    {
        cleanup();
    }
}

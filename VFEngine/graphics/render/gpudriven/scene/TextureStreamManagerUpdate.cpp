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

namespace render::gpudriven
{
    bool TextureStreamManager::createStreamableImage(
        StreamableTexture& tex,
        const resource::TextureStreamHeader& header,
        const std::vector<resource::MipLevelData>& tailMips)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        core::ImageInfoRequest imageInfo(
            vkDevice, device.getPhysicalDevice(),
            header.width, header.height, 1, header.mipLevels,
            tex.format, vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::ImageUtilities::createImage(imageInfo, tex.image, tex.allocation, device.getMemoryManager());

        tex.gpuMemoryUsage = estimateFullImageVRAM(header.width, header.height, header.mipLevels, tex.format);
        currentVRAMUsage += tex.gpuMemoryUsage;

        core::ImageViewInfoRequest viewInfo(
            vkDevice, tex.image, tex.format,
            vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2D, 1, header.mipLevels
        );
        core::ImageUtilities::createImageView(viewInfo, tex.view);

        vk::CommandBufferAllocateInfo cmdAllocInfo{};
        cmdAllocInfo.level = vk::CommandBufferLevel::ePrimary;
        cmdAllocInfo.commandPool = commandPool;
        cmdAllocInfo.commandBufferCount = 1;
        auto cmdBuffers = vkDevice.allocateCommandBuffers(cmdAllocInfo);
        vk::CommandBuffer cmd = cmdBuffers[0];

        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        cmd.begin(beginInfo);

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

        size_t totalTailSize = 0;
        for (const auto& mip : tailMips)
            totalTailSize += mip.dataSize;
        if (totalTailSize > stagingBufferSize)
            createStagingBuffer(totalTailSize * 2);

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
        device.submitGraphics(submitInfo);
        device.waitGraphicsIdle();

        vkDevice.freeCommandBuffers(commandPool, cmd);

        tex.currentSampler = createMipClampedSampler(tex.lowestLoadedMip, header.mipLevels - 1);

        return true;
    }

    bool TextureStreamManager::createTailOnlyImage(
        StreamableTexture& tex,
        uint32_t /*startMip*/,
        const std::vector<resource::MipLevelData>& retainedMips)
    {
        // VK-1480: a small image holding ONLY the retained tail mips (source mip startMip -> image mip
        // 0). Base dims are the tail base (retainedMips[0]); all its levels are uploaded at creation so
        // it's a permanently-valid fallback with no streaming.
        vk::Device vkDevice = device.getLogicalDevice();

        const uint32_t baseW = retainedMips.front().width;
        const uint32_t baseH = retainedMips.front().height;
        const uint32_t levels = static_cast<uint32_t>(retainedMips.size());

        core::ImageInfoRequest imageInfo(
            vkDevice, device.getPhysicalDevice(),
            baseW, baseH, 1, levels,
            tex.format, vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::ImageUtilities::createImage(imageInfo, tex.image, tex.allocation, device.getMemoryManager());

        size_t retainedBytes = 0;
        for (const auto& mip : retainedMips)
            retainedBytes += mip.dataSize;
        tex.gpuMemoryUsage = retainedBytes;
        currentVRAMUsage += tex.gpuMemoryUsage;

        core::ImageViewInfoRequest viewInfo(
            vkDevice, tex.image, tex.format,
            vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2D, 1, levels
        );
        core::ImageUtilities::createImageView(viewInfo, tex.view);

        vk::CommandBufferAllocateInfo cmdAllocInfo{};
        cmdAllocInfo.level = vk::CommandBufferLevel::ePrimary;
        cmdAllocInfo.commandPool = commandPool;
        cmdAllocInfo.commandBufferCount = 1;
        auto cmdBuffers = vkDevice.allocateCommandBuffers(cmdAllocInfo);
        vk::CommandBuffer cmd = cmdBuffers[0];

        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        cmd.begin(beginInfo);

        vk::ImageMemoryBarrier barrier{};
        barrier.oldLayout = vk::ImageLayout::eUndefined;
        barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = tex.image;
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = levels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                            vk::PipelineStageFlagBits::eTransfer,
                            {}, nullptr, nullptr, barrier);

        if (retainedBytes > stagingBufferSize)
            createStagingBuffer(retainedBytes * 2);

        size_t stagingOffset = 0;
        for (uint32_t i = 0; i < levels; ++i)
        {
            const auto& mip = retainedMips[i];
            memcpy(static_cast<char*>(stagingMapped) + stagingOffset, mip.data.data(), mip.dataSize);

            vk::BufferImageCopy copyRegion{};
            copyRegion.bufferOffset = static_cast<vk::DeviceSize>(stagingOffset);
            copyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            copyRegion.imageSubresource.mipLevel = i; // source mip startMip+i -> image mip i
            copyRegion.imageSubresource.baseArrayLayer = 0;
            copyRegion.imageSubresource.layerCount = 1;
            copyRegion.imageExtent = vk::Extent3D{mip.width, mip.height, 1};

            cmd.copyBufferToImage(stagingBuffer, tex.image,
                                  vk::ImageLayout::eTransferDstOptimal, copyRegion);

            stagingOffset += mip.dataSize;
        }

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
        device.submitGraphics(submitInfo);
        device.waitGraphicsIdle();

        vkDevice.freeCommandBuffers(commandPool, cmd);

        tex.currentSampler = createMipClampedSampler(0, levels - 1);

        return true;
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

        stats.totalRegistered = static_cast<uint32_t>(textures.size());
        stats.pendingReads = static_cast<uint32_t>(pendingReads.size());
        stats.vramUsedBytes = currentVRAMUsage;

        uint32_t fullyLoaded = 0;
        uint32_t partiallyLoaded = 0;
        uint32_t tailOnlyCount = 0;
        size_t tailOnlyBytesSaved = 0;
        for (const auto& [path, tex] : textures)
        {
            if (tex.tailOnly)
            {
                ++tailOnlyCount;
                // Bytes the full pyramid would have cost minus the tiny tail actually held.
                auto hIt = streamHandles.find(path);
                if (hIt != streamHandles.end())
                {
                    const auto& hdr = hIt->second->getHeader();
                    const size_t full = estimateFullImageVRAM(hdr.width, hdr.height, hdr.mipLevels, tex.format);
                    if (full > tex.gpuMemoryUsage)
                        tailOnlyBytesSaved += full - tex.gpuMemoryUsage;
                }
                continue;
            }
            if (tex.lowestLoadedMip == 0) fullyLoaded++;
            else partiallyLoaded++;
        }
        stats.fullyLoaded = fullyLoaded;
        stats.partiallyLoaded = partiallyLoaded;
        stats.tailOnlyCount = tailOnlyCount;
        stats.tailOnlyBytesSaved = tailOnlyBytesSaved;
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

        if (!excess.empty())
        {
            std::lock_guard lock(uploadQueueMutex);
            for (auto& e : excess)
                uploadQueue.push_back(std::move(e));
        }

        if (!validUploads.empty())
        {
            size_t totalSize = 0;
            for (const auto& up : validUploads)
                totalSize += up.mipData.dataSize;
            if (totalSize > stagingBufferSize)
                createStagingBuffer(totalSize * 2);

            vk::Device vkDevice = device.getLogicalDevice();

            vk::CommandBufferAllocateInfo cmdAllocInfo{};
            cmdAllocInfo.level = vk::CommandBufferLevel::ePrimary;
            cmdAllocInfo.commandPool = commandPool;
            cmdAllocInfo.commandBufferCount = 1;
            auto cmdBuffers = vkDevice.allocateCommandBuffers(cmdAllocInfo);
            vk::CommandBuffer cmd = cmdBuffers[0];

            vk::CommandBufferBeginInfo beginInfo{};
            beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            cmd.begin(beginInfo);

            size_t stagingOffset = 0;
            for (const auto& up : validUploads)
            {
                memcpy(static_cast<char*>(stagingMapped) + stagingOffset,
                       up.mipData.data.data(), up.mipData.dataSize);

                auto texIt = textures.find(up.path);
                auto& tex = texIt->second;

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

            vk::SubmitInfo submitInfo{};
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &cmd;
            device.submitGraphics(submitInfo);
            device.waitGraphicsIdle();

            vkDevice.freeCommandBuffers(commandPool, cmd);

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

    void TextureStreamManager::updatePriorities(const glm::vec3& cameraPos)
    {
        for (auto& [path, tex] : textures)
        {
            tex.lastAccessFrame = currentFrame;
            if (tex.distanceToCamera >= std::numeric_limits<float>::max())
                tex.distanceToCamera = 10000.0f;
        }
    }

    void TextureStreamManager::submitReadRequests()
    {
        std::priority_queue<TextureStreamPriorityEntry> priorityQueue;

        for (const auto& [path, tex] : textures)
        {
            if (tex.tailOnly) continue; // VK-1480: tail-only fallbacks are fully resident, never stream
            uint32_t desiredMip = calculateDesiredMip(tex.distanceToCamera, tex.totalMipLevels);
            if (desiredMip < tex.lowestLoadedMip)
            {
                float priority = calculatePriority(tex.lowestLoadedMip, desiredMip, tex.distanceToCamera);
                priorityQueue.push({path, desiredMip, priority});
            }
        }

        uint32_t readsSubmitted = 0;
        constexpr uint32_t maxReadsPerFrame = 4;

        while (!priorityQueue.empty() && readsSubmitted < maxReadsPerFrame)
        {
            auto entry = priorityQueue.top();
            priorityQueue.pop();

            auto texIt = textures.find(entry.path);
            if (texIt == textures.end()) continue;

            auto& tex = texIt->second;
            uint32_t nextMip = tex.lowestLoadedMip - 1;

            auto handleIt = streamHandles.find(entry.path);
            if (handleIt == streamHandles.end()) continue;

            auto readKey = std::make_pair(entry.path, nextMip);
            if (inFlightReads.contains(readKey)) continue;

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
        if (usageRatio < config.evictionThreshold) return;

        struct EvictionCandidate
        {
            std::string path;
            float score;
        };

        std::vector<EvictionCandidate> candidates;
        for (const auto& [path, tex] : textures)
        {
            if (tex.tailOnly) continue; // VK-1480: never evict tail-only fallbacks
            if (tex.lowestLoadedMip < tex.totalMipLevels - config.tailMipCount)
            {
                float ageFactor = static_cast<float>(currentFrame - tex.lastAccessFrame + 1);
                candidates.push_back({path, tex.distanceToCamera * ageFactor});
            }
        }

        std::sort(candidates.begin(), candidates.end(),
                  [](const auto& a, const auto& b) { return a.score > b.score; });

        uint32_t evictions = 0;
        for (const auto& candidate : candidates)
        {
            if (static_cast<float>(currentVRAMUsage) / static_cast<float>(config.vramBudgetBytes) <= config.evictionTarget)
                break;

            auto texIt = textures.find(candidate.path);
            if (texIt == textures.end()) continue;

            auto& tex = texIt->second;

            uint32_t newLowestMip = tex.totalMipLevels - config.tailMipCount;
            if (newLowestMip > tex.lowestLoadedMip)
            {
                size_t freedBytes = 0;
                for (uint32_t m = tex.lowestLoadedMip; m < newLowestMip; ++m)
                    freedBytes += estimateMipVRAM(tex.width, tex.height, m, tex.format);

                currentVRAMUsage = (freedBytes <= currentVRAMUsage) ? currentVRAMUsage - freedBytes : 0;

                tex.lowestLoadedMip = newLowestMip;
                updateSamplerAndDescriptor(tex);
                evictions++;
            }
        }

        stats.evictionsThisFrame = evictions;
    }
}

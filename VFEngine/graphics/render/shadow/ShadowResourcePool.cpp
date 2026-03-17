#include "ShadowResourcePool.hpp"
#include "../../core/Device.hpp"
#include "../../core/Utilities.hpp"
#include "../../core/DeferredDeletionQueue.hpp"
#include "print/Log.hpp"

namespace render::shadow
{
    ShadowResourcePool::ShadowResourcePool(core::Device& device)
        : device(device)
    {
    }

    ShadowResourcePool::~ShadowResourcePool()
    {
        cleanup();
    }

    void ShadowResourcePool::init()
    {
        if (initialized)
        {
            return;
        }

        createSamplers();
        createPlaceholderResources();

        initialized = true;
    }

    void ShadowResourcePool::cleanup()
    {
        if (!initialized)
            return;

        const auto& logicalDevice = device.getLogicalDevice();
        logicalDevice.waitIdle();

        freeAll();
        cleanupPlaceholders();
        cleanupSamplers();

        initialized = false;
    }

    void ShadowResourcePool::createSamplers()
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
        samplerInfo.compareEnable = VK_TRUE;
        samplerInfo.compareOp = vk::CompareOp::eLessOrEqual;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;
        samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        cubeComparisonSampler = logicalDevice.createSampler(samplerInfo);

        samplerInfo.magFilter = vk::Filter::eNearest;
        samplerInfo.minFilter = vk::Filter::eNearest;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = vk::CompareOp::eNever;

        cubeDepthSampler = logicalDevice.createSampler(samplerInfo);
    }

    void ShadowResourcePool::cleanupSamplers()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        if (cubeDepthSampler)
        {
            logicalDevice.destroySampler(cubeDepthSampler);
            cubeDepthSampler = nullptr;
        }
        if (cubeComparisonSampler)
        {
            logicalDevice.destroySampler(cubeComparisonSampler);
            cubeComparisonSampler = nullptr;
        }
    }

    ShadowResourceHandle ShadowResourcePool::allocateCube(uint32_t size)
    {
        ShadowResourceHandle handle;
        handle.resourceType = ShadowResourceType::Cube;
        handle.layerOrFace = 0;

        auto cubeMap = std::make_unique<ShadowCubeMap>(device);
        cubeMap->init(size);

        uint32_t index;
        if (!freeCubeIndices.empty())
        {
            index = freeCubeIndices.back();
            freeCubeIndices.pop_back();
            cubeMaps[index].resource = std::move(cubeMap);
            cubeMaps[index].allocated = true;
        }
        else
        {
            index = static_cast<uint32_t>(cubeMaps.size());
            CubeEntry entry;
            entry.resource = std::move(cubeMap);
            entry.allocated = true;
            cubeMaps.push_back(std::move(entry));
        }

        handle.resourceIndex = index;
        return handle;
    }

    void ShadowResourcePool::free(const ShadowResourceHandle& handle)
    {
        if (!handle.isValid())
            return;

        if (handle.isCube())
        {
            if (handle.resourceIndex < cubeMaps.size() && cubeMaps[handle.resourceIndex].allocated)
            {
                if (deletionQueue)
                {
                    auto extracted = cubeMaps[handle.resourceIndex].resource->extractResources();
                    std::vector<vk::ImageView> views;
                    views.reserve(extracted.faceViews.size() + 1);
                    if (extracted.cubeView)
                        views.push_back(extracted.cubeView);
                    for (auto view : extracted.faceViews)
                    {
                        if (view)
                            views.push_back(view);
                    }
                    deletionQueue->queueImage(extracted.image, extracted.memory, views);

                    for (auto fb : extracted.framebuffers)
                    {
                        if (fb)
                            deletionQueue->queueFramebuffer(fb);
                    }
                }
                else
                {
                    cubeMaps[handle.resourceIndex].resource->cleanup();
                }

                cubeMaps[handle.resourceIndex].resource.reset();
                cubeMaps[handle.resourceIndex].allocated = false;
                freeCubeIndices.push_back(handle.resourceIndex);
            }
        }
    }

    void ShadowResourcePool::setDeletionQueue(core::DeferredDeletionQueue* queue)
    {
        deletionQueue = queue;
    }

    void ShadowResourcePool::freeAll()
    {
        for (auto& entry : cubeMaps)
        {
            if (entry.allocated && entry.resource)
            {
                entry.resource->cleanup();
                entry.resource.reset();
                entry.allocated = false;
            }
        }
        cubeMaps.clear();
        freeCubeIndices.clear();
    }

    ShadowCubeMap* ShadowResourcePool::getCube(const ShadowResourceHandle& handle)
    {
        if (!handle.isValid() || !handle.isCube())
            return nullptr;

        if (handle.resourceIndex >= cubeMaps.size())
            return nullptr;

        auto& entry = cubeMaps[handle.resourceIndex];
        if (!entry.allocated || !entry.resource)
            return nullptr;

        return entry.resource.get();
    }

    const ShadowCubeMap* ShadowResourcePool::getCube(const ShadowResourceHandle& handle) const
    {
        if (!handle.isValid() || !handle.isCube())
            return nullptr;

        if (handle.resourceIndex >= cubeMaps.size())
            return nullptr;

        const auto& entry = cubeMaps[handle.resourceIndex];
        if (!entry.allocated || !entry.resource)
            return nullptr;

        return entry.resource.get();
    }

    uint32_t ShadowResourcePool::getActiveCubeCount() const
    {
        uint32_t count = 0;
        for (const auto& entry : cubeMaps)
        {
            if (entry.allocated)
                ++count;
        }
        return count;
    }

    void ShadowResourcePool::createPlaceholderResources()
    {
        placeholderCube = std::make_unique<ShadowCubeMap>(device);
        placeholderCube->init(1);

        const auto& logicalDevice = device.getLogicalDevice();
        auto cmd = core::Utilities::beginSingleTimeCommands(logicalDevice, device.getStagingCommandPool());

        vk::ImageMemoryBarrier cubeBarrier{};
        cubeBarrier.srcAccessMask = {};
        cubeBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        cubeBarrier.oldLayout = vk::ImageLayout::eUndefined;
        cubeBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        cubeBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        cubeBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        cubeBarrier.image = placeholderCube->getImage();
        cubeBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
        cubeBarrier.subresourceRange.baseMipLevel = 0;
        cubeBarrier.subresourceRange.levelCount = 1;
        cubeBarrier.subresourceRange.baseArrayLayer = 0;
        cubeBarrier.subresourceRange.layerCount = 6;

        cmd->pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eFragmentShader,
            {},
            0, nullptr,
            0, nullptr,
            1, &cubeBarrier
        );

        core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), cmd, nullptr);
    }

    void ShadowResourcePool::cleanupPlaceholders()
    {
        if (placeholderCube)
        {
            placeholderCube->cleanup();
            placeholderCube.reset();
        }
    }

    vk::ImageView ShadowResourcePool::getPlaceholderCubeView() const
    {
        if (placeholderCube && placeholderCube->isInitialized())
        {
            return placeholderCube->getCubeView();
        }
        return nullptr;
    }
}

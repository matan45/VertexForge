#include "ShadowResourcePool.hpp"
#include "ShadowSamplers.hpp"
#include "../../core/Device.hpp"
#include "../../core/Utilities.hpp"
#include "../../core/DeferredDeletionQueue.hpp"
#include <spdlog/spdlog.h>

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
            spdlog::warn("ShadowResourcePool::init() called when already initialized");
            return;
        }

        createSamplers();
        createDescriptorLayouts();
        createPlaceholderResources();

        initialized = true;
        spdlog::info("ShadowResourcePool initialized");
    }

    void ShadowResourcePool::cleanup()
    {
        if (!initialized)
            return;

        const auto& logicalDevice = device.getLogicalDevice();
        logicalDevice.waitIdle();

        // Free all resources
        freeAll();

        // Cleanup placeholders
        cleanupPlaceholders();

        // Cleanup descriptors
        cleanupDescriptors();

        // Cleanup samplers
        cleanupSamplers();

        initialized = false;
        spdlog::info("ShadowResourcePool cleaned up");
    }

    void ShadowResourcePool::recreate()
    {
        // Store existing allocations info for potential re-creation
        // (Currently just cleanup and reinit)
        cleanup();
        init();
    }

    void ShadowResourcePool::createSamplers()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        standardSampler = ShadowSamplers::createStandardSampler(logicalDevice);
        comparisonSampler = ShadowSamplers::createComparisonSampler(logicalDevice);
        cubeComparisonSampler = ShadowSamplers::createCubeComparisonSampler(logicalDevice);

        spdlog::debug("ShadowResourcePool: Created samplers");
    }

    void ShadowResourcePool::cleanupSamplers()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        if (cubeComparisonSampler)
        {
            logicalDevice.destroySampler(cubeComparisonSampler);
            cubeComparisonSampler = nullptr;
        }
        if (comparisonSampler)
        {
            logicalDevice.destroySampler(comparisonSampler);
            comparisonSampler = nullptr;
        }
        if (standardSampler)
        {
            logicalDevice.destroySampler(standardSampler);
            standardSampler = nullptr;
        }
    }

    void ShadowResourcePool::createDescriptorLayouts()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        // Layout for depth arrays (CSM)
        // Binding 0: sampler2DArrayShadow
        {
            vk::DescriptorSetLayoutBinding binding{};
            binding.binding = 0;
            binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            binding.descriptorCount = 1;
            binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings = &binding;

            arrayDescriptorLayout = logicalDevice.createDescriptorSetLayout(layoutInfo);
        }

        // Layout for cube maps (point lights)
        // Binding 0: samplerCubeShadow
        {
            vk::DescriptorSetLayoutBinding binding{};
            binding.binding = 0;
            binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            binding.descriptorCount = 1;
            binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings = &binding;

            cubeDescriptorLayout = logicalDevice.createDescriptorSetLayout(layoutInfo);
        }

        spdlog::debug("ShadowResourcePool: Created descriptor layouts");
    }

    void ShadowResourcePool::cleanupDescriptors()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        // Note: We only provide layouts. Descriptor sets are allocated by the
        // rendering code (e.g., ShadowSystem or forward shading pipeline) using
        // their own pools for proper lifetime management.
        if (cubeDescriptorLayout)
        {
            logicalDevice.destroyDescriptorSetLayout(cubeDescriptorLayout);
            cubeDescriptorLayout = nullptr;
        }
        if (arrayDescriptorLayout)
        {
            logicalDevice.destroyDescriptorSetLayout(arrayDescriptorLayout);
            arrayDescriptorLayout = nullptr;
        }
    }

    ShadowResourceHandle ShadowResourcePool::allocateArray(uint32_t width, uint32_t height, uint32_t layers)
    {
        ShadowResourceHandle handle;
        handle.resourceType = ShadowResourceType::Array;
        handle.layerOrFace = 0;

        // Create new depth array
        auto depthArray = std::make_unique<ShadowDepthArray>(device);
        depthArray->init(width, height, layers);

        // Try to reuse a freed index first
        uint32_t index;
        if (!freeArrayIndices.empty())
        {
            index = freeArrayIndices.back();
            freeArrayIndices.pop_back();

            // Reuse existing slot
            depthArrays[index].resource = std::move(depthArray);
            depthArrays[index].allocated = true;
        }
        else
        {
            // Allocate new slot
            index = static_cast<uint32_t>(depthArrays.size());

            ArrayEntry entry;
            entry.resource = std::move(depthArray);
            entry.allocated = true;
            depthArrays.push_back(std::move(entry));
        }

        handle.resourceIndex = index;

        spdlog::debug("ShadowResourcePool: Allocated depth array {} ({}x{} x {} layers)",
                      index, width, height, layers);

        return handle;
    }

    ShadowResourceHandle ShadowResourcePool::allocateCube(uint32_t size)
    {
        ShadowResourceHandle handle;
        handle.resourceType = ShadowResourceType::Cube;
        handle.layerOrFace = 0;

        // Create new cube map
        auto cubeMap = std::make_unique<ShadowCubeMap>(device);
        cubeMap->init(size);

        // Try to reuse a freed index first
        uint32_t index;
        if (!freeCubeIndices.empty())
        {
            index = freeCubeIndices.back();
            freeCubeIndices.pop_back();

            // Reuse existing slot
            cubeMaps[index].resource = std::move(cubeMap);
            cubeMaps[index].allocated = true;
        }
        else
        {
            // Allocate new slot
            index = static_cast<uint32_t>(cubeMaps.size());

            CubeEntry entry;
            entry.resource = std::move(cubeMap);
            entry.allocated = true;
            cubeMaps.push_back(std::move(entry));
        }

        handle.resourceIndex = index;

        spdlog::debug("ShadowResourcePool: Allocated cube map {} ({}x{} x 6 faces)",
                      index, size, size);

        return handle;
    }

    void ShadowResourcePool::free(const ShadowResourceHandle& handle)
    {
        if (!handle.isValid())
            return;

        if (handle.isArray())
        {
            if (handle.resourceIndex < depthArrays.size() && depthArrays[handle.resourceIndex].allocated)
            {
                if (deletionQueue)
                {
                    // Extract resources and queue for deferred deletion
                    auto extracted = depthArrays[handle.resourceIndex].resource->extractResources();

                    // Queue the image and memory
                    std::vector<vk::ImageView> views;
                    views.reserve(extracted.layerViews.size() + 1);
                    if (extracted.arrayView)
                        views.push_back(extracted.arrayView);
                    for (auto view : extracted.layerViews)
                    {
                        if (view)
                            views.push_back(view);
                    }

                    deletionQueue->queueImage(extracted.image, extracted.memory, views);
                    spdlog::debug("ShadowResourcePool: Queued depth array {} for deferred deletion", handle.resourceIndex);
                }
                else
                {
                    // Fallback to immediate cleanup (caller must ensure GPU synchronization)
                    depthArrays[handle.resourceIndex].resource->cleanup();
                    spdlog::debug("ShadowResourcePool: Freed depth array {} (immediate)", handle.resourceIndex);
                }

                depthArrays[handle.resourceIndex].resource.reset();
                depthArrays[handle.resourceIndex].allocated = false;

                // Add index to free list for recycling
                freeArrayIndices.push_back(handle.resourceIndex);
            }
        }
        else if (handle.isCube())
        {
            if (handle.resourceIndex < cubeMaps.size() && cubeMaps[handle.resourceIndex].allocated)
            {
                if (deletionQueue)
                {
                    // Extract resources and queue for deferred deletion
                    auto extracted = cubeMaps[handle.resourceIndex].resource->extractResources();

                    // Queue the image and memory
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
                    spdlog::debug("ShadowResourcePool: Queued cube map {} for deferred deletion", handle.resourceIndex);
                }
                else
                {
                    // Fallback to immediate cleanup (caller must ensure GPU synchronization)
                    cubeMaps[handle.resourceIndex].resource->cleanup();
                    spdlog::debug("ShadowResourcePool: Freed cube map {} (immediate)", handle.resourceIndex);
                }

                cubeMaps[handle.resourceIndex].resource.reset();
                cubeMaps[handle.resourceIndex].allocated = false;

                // Add index to free list for recycling
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
        for (auto& entry : depthArrays)
        {
            if (entry.allocated && entry.resource)
            {
                entry.resource->cleanup();
                entry.resource.reset();
                entry.allocated = false;
            }
        }
        depthArrays.clear();
        freeArrayIndices.clear();

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

        spdlog::debug("ShadowResourcePool: Freed all resources");
    }

    ShadowDepthArray* ShadowResourcePool::getArray(const ShadowResourceHandle& handle)
    {
        if (!handle.isValid() || !handle.isArray())
            return nullptr;

        if (handle.resourceIndex >= depthArrays.size())
            return nullptr;

        auto& entry = depthArrays[handle.resourceIndex];
        if (!entry.allocated || !entry.resource)
            return nullptr;

        return entry.resource.get();
    }

    const ShadowDepthArray* ShadowResourcePool::getArray(const ShadowResourceHandle& handle) const
    {
        if (!handle.isValid() || !handle.isArray())
            return nullptr;

        if (handle.resourceIndex >= depthArrays.size())
            return nullptr;

        const auto& entry = depthArrays[handle.resourceIndex];
        if (!entry.allocated || !entry.resource)
            return nullptr;

        return entry.resource.get();
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

    uint32_t ShadowResourcePool::getActiveArrayCount() const
    {
        uint32_t count = 0;
        for (const auto& entry : depthArrays)
        {
            if (entry.allocated)
                ++count;
        }
        return count;
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
        // Create a 1x1x1 placeholder depth array for binding when no CSM lights exist
        placeholderArray = std::make_unique<ShadowDepthArray>(device);
        placeholderArray->init(1, 1, 1);

        // Create a 1x1 placeholder cube map for binding when no point light shadows exist
        placeholderCube = std::make_unique<ShadowCubeMap>(device);
        placeholderCube->init(1);

        // Transition both placeholders to shader-read-optimal layout using a one-time command buffer
        const auto& logicalDevice = device.getLogicalDevice();
        auto cmd = core::Utilities::beginSingleTimeCommands(logicalDevice, device.getStagingCommandPool());

        // Transition placeholder array from undefined to shader read optimal
        vk::ImageMemoryBarrier barrier{};
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.oldLayout = vk::ImageLayout::eUndefined;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = placeholderArray->getImage();
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

        // Transition placeholder cube from undefined to shader read optimal (6 layers for cube faces)
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
        cubeBarrier.subresourceRange.layerCount = 6;  // All 6 cube faces

        cmd->pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eFragmentShader,
            {},
            0, nullptr,
            0, nullptr,
            1, &cubeBarrier
        );

        core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), cmd, nullptr);

        spdlog::debug("ShadowResourcePool: Created placeholder depth array and cube map");
    }

    void ShadowResourcePool::cleanupPlaceholders()
    {
        if (placeholderArray)
        {
            placeholderArray->cleanup();
            placeholderArray.reset();
        }
        if (placeholderCube)
        {
            placeholderCube->cleanup();
            placeholderCube.reset();
        }
    }

    vk::ImageView ShadowResourcePool::getPlaceholderArrayView() const
    {
        if (placeholderArray && placeholderArray->isInitialized())
        {
            return placeholderArray->getArrayView();
        }
        return nullptr;
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

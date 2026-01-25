#include "ShadowResourcePool.hpp"
#include "ShadowSamplers.hpp"
#include "../../core/Device.hpp"
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

        if (descriptorPool)
        {
            logicalDevice.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }
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

        // Store in pool
        uint32_t index = nextArrayIndex++;
        handle.resourceIndex = index;

        ArrayEntry entry;
        entry.resource = std::move(depthArray);
        entry.allocated = true;

        if (index < depthArrays.size())
        {
            depthArrays[index] = std::move(entry);
        }
        else
        {
            depthArrays.push_back(std::move(entry));
        }

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

        // Store in pool
        uint32_t index = nextCubeIndex++;
        handle.resourceIndex = index;

        CubeEntry entry;
        entry.resource = std::move(cubeMap);
        entry.allocated = true;

        if (index < cubeMaps.size())
        {
            cubeMaps[index] = std::move(entry);
        }
        else
        {
            cubeMaps.push_back(std::move(entry));
        }

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
                depthArrays[handle.resourceIndex].resource->cleanup();
                depthArrays[handle.resourceIndex].resource.reset();
                depthArrays[handle.resourceIndex].allocated = false;
                spdlog::debug("ShadowResourcePool: Freed depth array {}", handle.resourceIndex);
            }
        }
        else if (handle.isCube())
        {
            if (handle.resourceIndex < cubeMaps.size() && cubeMaps[handle.resourceIndex].allocated)
            {
                cubeMaps[handle.resourceIndex].resource->cleanup();
                cubeMaps[handle.resourceIndex].resource.reset();
                cubeMaps[handle.resourceIndex].allocated = false;
                spdlog::debug("ShadowResourcePool: Freed cube map {}", handle.resourceIndex);
            }
        }
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
        nextArrayIndex = 0;

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
        nextCubeIndex = 0;

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
}

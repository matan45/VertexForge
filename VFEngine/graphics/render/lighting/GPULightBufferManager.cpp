#include "GPULightBufferManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/Log.hpp"
#include <cstring>

namespace render::lighting
{
    GPULightBufferManager::GPULightBufferManager(core::Device& device)
        : device(device)
    {
    }

    GPULightBufferManager::~GPULightBufferManager()
    {
        cleanup();
    }

    void GPULightBufferManager::init()
    {
        if (initialized)
        {
            return;
        }

        cpuDirectionalLights.resize(LightConstants::MAX_DIRECTIONAL_LIGHTS);
        cpuPointLights.resize(LightConstants::MAX_POINT_LIGHTS);
        cpuSpotLights.resize(LightConstants::MAX_SPOT_LIGHTS);

        vfLogInfo("GPULightBufferManager: Initializing with max {} directional, {} point, {} spot lights",
                   LightConstants::MAX_DIRECTIONAL_LIGHTS,
                   LightConstants::MAX_POINT_LIGHTS,
                   LightConstants::MAX_SPOT_LIGHTS);

        createBuffers();
        createDescriptorSetLayout();
        createDescriptorPool();
        allocateDescriptorSet();
        updateDescriptors();

        initialized = true;
    }

    void GPULightBufferManager::cleanup()
    {
        if (!initialized)
        {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        if (descriptorPool)
        {
            vkDevice.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }

        if (descriptorSetLayout)
        {
            vkDevice.destroyDescriptorSetLayout(descriptorSetLayout);
            descriptorSetLayout = nullptr;
        }

        destroyBuffers();

        cpuDirectionalLights.clear();
        cpuPointLights.clear();
        cpuSpotLights.clear();

        initialized = false;
    }

    void GPULightBufferManager::createBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        {
            vk::DeviceSize bufferSize = LightConstants::MAX_DIRECTIONAL_LIGHTS * sizeof(GPUDirectionalLight);

            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = bufferSize;
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, directionalBuffer, directionalMemory);

            for (auto& sf : stagingFrames)
            {
                core::BufferInfoRequest stagingReq(logicalDevice, physicalDevice);
                stagingReq.size = bufferSize;
                stagingReq.usage = vk::BufferUsageFlagBits::eTransferSrc;
                stagingReq.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
                core::BufferUtilities::createBuffer(stagingReq, sf.directionalStagingBuffer, sf.directionalStagingMemory);
                sf.directionalStagingMapped = logicalDevice.mapMemory(sf.directionalStagingMemory, 0, bufferSize, vk::MemoryMapFlags{});
            }
        }

        {
            vk::DeviceSize bufferSize = LightConstants::MAX_POINT_LIGHTS * sizeof(GPUPointLight);

            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = bufferSize;
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, pointBuffer, pointMemory);

            for (auto& sf : stagingFrames)
            {
                core::BufferInfoRequest stagingReq(logicalDevice, physicalDevice);
                stagingReq.size = bufferSize;
                stagingReq.usage = vk::BufferUsageFlagBits::eTransferSrc;
                stagingReq.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
                core::BufferUtilities::createBuffer(stagingReq, sf.pointStagingBuffer, sf.pointStagingMemory);
                sf.pointStagingMapped = logicalDevice.mapMemory(sf.pointStagingMemory, 0, bufferSize, vk::MemoryMapFlags{});
            }
        }

        {
            vk::DeviceSize bufferSize = LightConstants::MAX_SPOT_LIGHTS * sizeof(GPUSpotLight);

            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = bufferSize;
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, spotBuffer, spotMemory);

            for (auto& sf : stagingFrames)
            {
                core::BufferInfoRequest stagingReq(logicalDevice, physicalDevice);
                stagingReq.size = bufferSize;
                stagingReq.usage = vk::BufferUsageFlagBits::eTransferSrc;
                stagingReq.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
                core::BufferUtilities::createBuffer(stagingReq, sf.spotStagingBuffer, sf.spotStagingMemory);
                sf.spotStagingMapped = logicalDevice.mapMemory(sf.spotStagingMemory, 0, bufferSize, vk::MemoryMapFlags{});
            }
        }

        {
            vk::DeviceSize bufferSize = sizeof(GPULightCounts);

            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = bufferSize;
            request.usage = vk::BufferUsageFlagBits::eUniformBuffer;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(request, countsBuffer, countsMemory);

            countsMapped = logicalDevice.mapMemory(countsMemory, 0, bufferSize, vk::MemoryMapFlags{});

            GPULightCounts counts{0, 0, 0, 0.5f};
            std::memcpy(countsMapped, &counts, sizeof(GPULightCounts));
        }

    }

    void GPULightBufferManager::destroyBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        for (auto& sf : stagingFrames)
        {
            if (sf.directionalStagingMapped) { logicalDevice.unmapMemory(sf.directionalStagingMemory); sf.directionalStagingMapped = nullptr; }
            core::BufferUtilities::destroyBuffer(logicalDevice, sf.directionalStagingBuffer, sf.directionalStagingMemory);

            if (sf.pointStagingMapped) { logicalDevice.unmapMemory(sf.pointStagingMemory); sf.pointStagingMapped = nullptr; }
            core::BufferUtilities::destroyBuffer(logicalDevice, sf.pointStagingBuffer, sf.pointStagingMemory);

            if (sf.spotStagingMapped) { logicalDevice.unmapMemory(sf.spotStagingMemory); sf.spotStagingMapped = nullptr; }
            core::BufferUtilities::destroyBuffer(logicalDevice, sf.spotStagingBuffer, sf.spotStagingMemory);
        }

        if (countsMapped)
        {
            logicalDevice.unmapMemory(countsMemory);
            countsMapped = nullptr;
        }

        core::BufferUtilities::destroyBuffer(logicalDevice, directionalBuffer, directionalMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, pointBuffer, pointMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, spotBuffer, spotMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, countsBuffer, countsMemory);
    }

    void GPULightBufferManager::createDescriptorSetLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayoutBinding, 4> bindings{};

        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment |
                                 vk::ShaderStageFlagBits::eCompute |
                                 vk::ShaderStageFlagBits::eMeshEXT;
        bindings[0].pImmutableSamplers = nullptr;

        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment |
                                 vk::ShaderStageFlagBits::eCompute |
                                 vk::ShaderStageFlagBits::eMeshEXT;
        bindings[1].pImmutableSamplers = nullptr;

        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eFragment |
                                 vk::ShaderStageFlagBits::eCompute |
                                 vk::ShaderStageFlagBits::eMeshEXT;
        bindings[2].pImmutableSamplers = nullptr;

        bindings[3].binding = 3;
        bindings[3].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = vk::ShaderStageFlagBits::eFragment |
                                 vk::ShaderStageFlagBits::eCompute |
                                 vk::ShaderStageFlagBits::eMeshEXT;
        bindings[3].pImmutableSamplers = nullptr;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = vkDevice.createDescriptorSetLayout(layoutInfo);
    }

    void GPULightBufferManager::createDescriptorPool()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[0].descriptorCount = 3;
        poolSizes[1].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[1].descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        descriptorPool = vkDevice.createDescriptorPool(poolInfo);
    }

    void GPULightBufferManager::allocateDescriptorSet()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        std::vector<vk::DescriptorSet> sets = vkDevice.allocateDescriptorSets(allocInfo);
        descriptorSet = sets[0];

    }

    void GPULightBufferManager::updateDescriptors()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorBufferInfo, 4> bufferInfos{};

        bufferInfos[0].buffer = directionalBuffer;
        bufferInfos[0].offset = 0;
        bufferInfos[0].range = VK_WHOLE_SIZE;

        bufferInfos[1].buffer = pointBuffer;
        bufferInfos[1].offset = 0;
        bufferInfos[1].range = VK_WHOLE_SIZE;

        bufferInfos[2].buffer = spotBuffer;
        bufferInfos[2].offset = 0;
        bufferInfos[2].range = VK_WHOLE_SIZE;

        bufferInfos[3].buffer = countsBuffer;
        bufferInfos[3].offset = 0;
        bufferInfos[3].range = sizeof(GPULightCounts);

        std::array<vk::WriteDescriptorSet, 4> descriptorWrites{};

        for (uint32_t i = 0; i < 4; ++i)
        {
            descriptorWrites[i].dstSet = descriptorSet;
            descriptorWrites[i].dstBinding = i;
            descriptorWrites[i].dstArrayElement = 0;
            descriptorWrites[i].descriptorType = (i < 3) ? vk::DescriptorType::eStorageBuffer : vk::DescriptorType::eUniformBuffer;
            descriptorWrites[i].descriptorCount = 1;
            descriptorWrites[i].pBufferInfo = &bufferInfos[i];
        }

        vkDevice.updateDescriptorSets(static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

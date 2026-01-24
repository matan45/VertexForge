#include "GPULightBufferManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/Logger.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include <cmath>
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

        loggerInfo("GPULightBufferManager: Initializing with max {} directional, {} point, {} spot lights",
                   LightConstants::MAX_DIRECTIONAL_LIGHTS,
                   LightConstants::MAX_POINT_LIGHTS,
                   LightConstants::MAX_SPOT_LIGHTS);

        createBuffers();
        createDescriptorSetLayout();
        createDescriptorPool();
        allocateDescriptorSet();
        updateDescriptors();

        initialized = true;
        loggerInfo("GPULightBufferManager: Initialized successfully");
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
        loggerInfo("GPULightBufferManager: Cleaned up");
    }

    void GPULightBufferManager::createBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        // Directional light buffers
        {
            vk::DeviceSize bufferSize = LightConstants::MAX_DIRECTIONAL_LIGHTS * sizeof(GPUDirectionalLight);

            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = bufferSize;
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, directionalBuffer, directionalMemory);

            request.usage = vk::BufferUsageFlagBits::eTransferSrc;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(request, directionalStagingBuffer, directionalStagingMemory);

            directionalStagingMapped = logicalDevice.mapMemory(directionalStagingMemory, 0, bufferSize, vk::MemoryMapFlags{});
        }

        // Point light buffers
        {
            vk::DeviceSize bufferSize = LightConstants::MAX_POINT_LIGHTS * sizeof(GPUPointLight);

            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = bufferSize;
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, pointBuffer, pointMemory);

            request.usage = vk::BufferUsageFlagBits::eTransferSrc;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(request, pointStagingBuffer, pointStagingMemory);

            pointStagingMapped = logicalDevice.mapMemory(pointStagingMemory, 0, bufferSize, vk::MemoryMapFlags{});
        }

        // Spot light buffers
        {
            vk::DeviceSize bufferSize = LightConstants::MAX_SPOT_LIGHTS * sizeof(GPUSpotLight);

            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = bufferSize;
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, spotBuffer, spotMemory);

            request.usage = vk::BufferUsageFlagBits::eTransferSrc;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(request, spotStagingBuffer, spotStagingMemory);

            spotStagingMapped = logicalDevice.mapMemory(spotStagingMemory, 0, bufferSize, vk::MemoryMapFlags{});
        }

        // Light counts UBO (HOST_VISIBLE for direct updates)
        {
            vk::DeviceSize bufferSize = sizeof(GPULightCounts);

            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = bufferSize;
            request.usage = vk::BufferUsageFlagBits::eUniformBuffer;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(request, countsBuffer, countsMemory);

            countsMapped = logicalDevice.mapMemory(countsMemory, 0, bufferSize, vk::MemoryMapFlags{});

            // Initialize to zero
            GPULightCounts counts{0, 0, 0, 0};
            std::memcpy(countsMapped, &counts, sizeof(GPULightCounts));
        }

        loggerInfo("GPULightBufferManager: Created light buffers");
    }

    void GPULightBufferManager::destroyBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        if (directionalStagingMapped)
        {
            logicalDevice.unmapMemory(directionalStagingMemory);
            directionalStagingMapped = nullptr;
        }
        if (pointStagingMapped)
        {
            logicalDevice.unmapMemory(pointStagingMemory);
            pointStagingMapped = nullptr;
        }
        if (spotStagingMapped)
        {
            logicalDevice.unmapMemory(spotStagingMemory);
            spotStagingMapped = nullptr;
        }
        if (countsMapped)
        {
            logicalDevice.unmapMemory(countsMemory);
            countsMapped = nullptr;
        }

        core::BufferUtilities::destroyBuffer(logicalDevice, directionalStagingBuffer, directionalStagingMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, directionalBuffer, directionalMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, pointStagingBuffer, pointStagingMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, pointBuffer, pointMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, spotStagingBuffer, spotStagingMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, spotBuffer, spotMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, countsBuffer, countsMemory);
    }

    void GPULightBufferManager::createDescriptorSetLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayoutBinding, 4> bindings{};

        // Binding 0: Directional lights SSBO
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment |
                                 vk::ShaderStageFlagBits::eCompute |
                                 vk::ShaderStageFlagBits::eMeshEXT;
        bindings[0].pImmutableSamplers = nullptr;

        // Binding 1: Point lights SSBO
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment |
                                 vk::ShaderStageFlagBits::eCompute |
                                 vk::ShaderStageFlagBits::eMeshEXT;
        bindings[1].pImmutableSamplers = nullptr;

        // Binding 2: Spot lights SSBO
        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eFragment |
                                 vk::ShaderStageFlagBits::eCompute |
                                 vk::ShaderStageFlagBits::eMeshEXT;
        bindings[2].pImmutableSamplers = nullptr;

        // Binding 3: Light counts UBO
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
        loggerInfo("GPULightBufferManager: Created descriptor set layout");
    }

    void GPULightBufferManager::createDescriptorPool()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[0].descriptorCount = 3; // 3 SSBOs
        poolSizes[1].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[1].descriptorCount = 1; // 1 UBO

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        descriptorPool = vkDevice.createDescriptorPool(poolInfo);
        loggerInfo("GPULightBufferManager: Created descriptor pool");
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

        loggerInfo("GPULightBufferManager: Allocated descriptor set");
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

    void GPULightBufferManager::updateFromScene()
    {
        if (!initialized)
        {
            return;
        }

        collectDirectionalLights();
        collectPointLights();
        collectSpotLights();
        updateCountsBuffer();

        needsUpload = true;
    }

    void GPULightBufferManager::collectDirectionalLights()
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::DirectionalLightComponent, components::TransformComponent>();

        directionalCount = 0;

        for (auto entity : view)
        {
            if (directionalCount >= LightConstants::MAX_DIRECTIONAL_LIGHTS)
            {
                loggerWarning("GPULightBufferManager: Exceeded max directional lights ({})", LightConstants::MAX_DIRECTIONAL_LIGHTS);
                break;
            }

            const auto& light = view.get<components::DirectionalLightComponent>(entity);
            const auto& transform = view.get<components::TransformComponent>(entity);

            // Calculate forward direction from rotation (Euler angles in degrees)
            float yawRad = glm::radians(transform.rotation.y);
            float pitchRad = glm::radians(transform.rotation.x);
            glm::vec3 direction;
            direction.x = -std::sin(yawRad) * std::cos(pitchRad);
            direction.y = std::sin(pitchRad);
            direction.z = -std::cos(yawRad) * std::cos(pitchRad);
            direction = glm::normalize(direction);

            GPUDirectionalLight& gpuLight = cpuDirectionalLights[directionalCount];
            gpuLight.direction = direction;
            gpuLight.intensity = light.intensity;
            gpuLight.color = light.color;
            gpuLight.padding = 0;

            ++directionalCount;
        }
    }

    void GPULightBufferManager::collectPointLights()
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::PointLightComponent, components::WorldTransformComponent>();

        pointCount = 0;

        for (auto entity : view)
        {
            if (pointCount >= LightConstants::MAX_POINT_LIGHTS)
            {
                loggerWarning("GPULightBufferManager: Exceeded max point lights ({})", LightConstants::MAX_POINT_LIGHTS);
                break;
            }

            const auto& light = view.get<components::PointLightComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            // Extract position from world matrix
            glm::vec3 position = glm::vec3(worldTransform.worldMatrix[3]);

            GPUPointLight& gpuLight = cpuPointLights[pointCount];
            gpuLight.position = position;
            gpuLight.radius = light.radius;
            gpuLight.color = light.color;
            gpuLight.intensity = light.intensity;

            ++pointCount;
        }
    }

    void GPULightBufferManager::collectSpotLights()
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::SpotLightComponent, components::TransformComponent, components::WorldTransformComponent>();

        spotCount = 0;

        for (auto entity : view)
        {
            if (spotCount >= LightConstants::MAX_SPOT_LIGHTS)
            {
                loggerWarning("GPULightBufferManager: Exceeded max spot lights ({})", LightConstants::MAX_SPOT_LIGHTS);
                break;
            }

            const auto& light = view.get<components::SpotLightComponent>(entity);
            const auto& transform = view.get<components::TransformComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            // Extract position from world matrix
            glm::vec3 position = glm::vec3(worldTransform.worldMatrix[3]);

            // Calculate forward direction from rotation
            float yawRad = glm::radians(transform.rotation.y);
            float pitchRad = glm::radians(transform.rotation.x);
            glm::vec3 direction;
            direction.x = -std::sin(yawRad) * std::cos(pitchRad);
            direction.y = std::sin(pitchRad);
            direction.z = -std::cos(yawRad) * std::cos(pitchRad);
            direction = glm::normalize(direction);

            GPUSpotLight& gpuLight = cpuSpotLights[spotCount];
            gpuLight.position = position;
            gpuLight.range = light.range;
            gpuLight.direction = direction;
            gpuLight.intensity = light.intensity;
            gpuLight.color = light.color;
            gpuLight.cosInnerAngle = std::cos(glm::radians(light.innerAngle));
            gpuLight.cosOuterAngle = std::cos(glm::radians(light.outerAngle));
            gpuLight.padding[0] = 0.0f;
            gpuLight.padding[1] = 0.0f;
            gpuLight.padding[2] = 0.0f;

            ++spotCount;
        }
    }

    void GPULightBufferManager::updateCountsBuffer()
    {
        if (!countsMapped)
        {
            return;
        }

        GPULightCounts counts{};
        counts.directionalCount = directionalCount;
        counts.pointCount = pointCount;
        counts.spotCount = spotCount;
        counts.padding = 0;

        std::memcpy(countsMapped, &counts, sizeof(GPULightCounts));
    }

    void GPULightBufferManager::uploadToGPU(vk::CommandBuffer cmd)
    {
        if (!initialized || !needsUpload)
        {
            return;
        }

        std::vector<vk::BufferCopy> copyRegions;

        // Copy directional lights to staging buffer and record copy command
        if (directionalCount > 0)
        {
            size_t copySize = directionalCount * sizeof(GPUDirectionalLight);
            std::memcpy(directionalStagingMapped, cpuDirectionalLights.data(), copySize);

            vk::BufferCopy region{};
            region.srcOffset = 0;
            region.dstOffset = 0;
            region.size = copySize;
            cmd.copyBuffer(directionalStagingBuffer, directionalBuffer, region);
        }

        // Copy point lights to staging buffer and record copy command
        if (pointCount > 0)
        {
            size_t copySize = pointCount * sizeof(GPUPointLight);
            std::memcpy(pointStagingMapped, cpuPointLights.data(), copySize);

            vk::BufferCopy region{};
            region.srcOffset = 0;
            region.dstOffset = 0;
            region.size = copySize;
            cmd.copyBuffer(pointStagingBuffer, pointBuffer, region);
        }

        // Copy spot lights to staging buffer and record copy command
        if (spotCount > 0)
        {
            size_t copySize = spotCount * sizeof(GPUSpotLight);
            std::memcpy(spotStagingMapped, cpuSpotLights.data(), copySize);

            vk::BufferCopy region{};
            region.srcOffset = 0;
            region.dstOffset = 0;
            region.size = copySize;
            cmd.copyBuffer(spotStagingBuffer, spotBuffer, region);
        }

        // Memory barrier to ensure transfers complete before shader reads
        std::array<vk::BufferMemoryBarrier, 3> barriers{};

        barriers[0].srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barriers[0].dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].buffer = directionalBuffer;
        barriers[0].offset = 0;
        barriers[0].size = VK_WHOLE_SIZE;

        barriers[1].srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barriers[1].dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].buffer = pointBuffer;
        barriers[1].offset = 0;
        barriers[1].size = VK_WHOLE_SIZE;

        barriers[2].srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barriers[2].dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barriers[2].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[2].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[2].buffer = spotBuffer;
        barriers[2].offset = 0;
        barriers[2].size = VK_WHOLE_SIZE;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader |
            vk::PipelineStageFlagBits::eComputeShader |
            vk::PipelineStageFlagBits::eMeshShaderEXT,
            {},
            {},
            barriers,
            {}
        );

        needsUpload = false;
    }
}

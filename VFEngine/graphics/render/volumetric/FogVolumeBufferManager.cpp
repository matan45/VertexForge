#include "FogVolumeBufferManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cstring>

namespace render::volumetric
{
    FogVolumeBufferManager::FogVolumeBufferManager(core::Device& device)
        : device(device)
    {
        cpuVolumes.reserve(MAX_FOG_VOLUMES);
    }

    FogVolumeBufferManager::~FogVolumeBufferManager()
    {
        if (initialized)
        {
            cleanup();
        }
    }

    void FogVolumeBufferManager::init()
    {
        if (initialized) return;

        createBuffer();
        createDescriptorSetLayout();
        createDescriptorPool();
        allocateDescriptorSet();
        updateDescriptors();

        // Initialize with zero count
        uint32_t header[4] = {0, 0, 0, 0};
        std::memcpy(mappedMemory, header, sizeof(header));

        initialized = true;
    }

    void FogVolumeBufferManager::cleanup()
    {
        if (!initialized) return;

        const auto& dev = device.getLogicalDevice();

        if (descriptorPool)
        {
            dev.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }
        if (descriptorSetLayout)
        {
            dev.destroyDescriptorSetLayout(descriptorSetLayout);
            descriptorSetLayout = nullptr;
        }

        destroyBuffer();
        initialized = false;
    }

    void FogVolumeBufferManager::createBuffer()
    {
        const auto& dev = device.getLogicalDevice();
        const auto& physDev = device.getPhysicalDevice();

        core::BufferInfoRequest request(dev, physDev);
        request.size = BUFFER_SIZE;
        request.usage = vk::BufferUsageFlagBits::eStorageBuffer;
        request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(request, fogVolumeBuffer, fogVolumeMemory);

        mappedMemory = dev.mapMemory(fogVolumeMemory, 0, BUFFER_SIZE, vk::MemoryMapFlags{});
    }

    void FogVolumeBufferManager::destroyBuffer()
    {
        const auto& dev = device.getLogicalDevice();

        if (mappedMemory)
        {
            dev.unmapMemory(fogVolumeMemory);
            mappedMemory = nullptr;
        }
        core::BufferUtilities::destroyBuffer(dev, fogVolumeBuffer, fogVolumeMemory);
    }

    void FogVolumeBufferManager::createDescriptorSetLayout()
    {
        const auto& dev = device.getLogicalDevice();

        vk::DescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = vk::DescriptorType::eStorageBuffer;
        binding.descriptorCount = 1;
        binding.stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;

        descriptorSetLayout = dev.createDescriptorSetLayout(layoutInfo);
    }

    void FogVolumeBufferManager::createDescriptorPool()
    {
        const auto& dev = device.getLogicalDevice();

        vk::DescriptorPoolSize poolSize{vk::DescriptorType::eStorageBuffer, 1};

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        descriptorPool = dev.createDescriptorPool(poolInfo);
    }

    void FogVolumeBufferManager::allocateDescriptorSet()
    {
        const auto& dev = device.getLogicalDevice();

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        descriptorSet = dev.allocateDescriptorSets(allocInfo)[0];
    }

    void FogVolumeBufferManager::updateDescriptors()
    {
        const auto& dev = device.getLogicalDevice();

        vk::DescriptorBufferInfo bufInfo{fogVolumeBuffer, 0, BUFFER_SIZE};

        vk::WriteDescriptorSet write{};
        write.dstSet = descriptorSet;
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eStorageBuffer;
        write.pBufferInfo = &bufInfo;

        dev.updateDescriptorSets(1, &write, 0, nullptr);
    }

    void FogVolumeBufferManager::updateFromScene()
    {
        cpuVolumes.clear();
        volumeCount = 0;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::FogVolumeComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            if (volumeCount >= MAX_FOG_VOLUMES) break;

            const auto& fog = view.get<components::FogVolumeComponent>(entity);
            const auto& world = view.get<components::WorldTransformComponent>(entity);

            GPUFogVolume gpuVol;

            // Compute world-to-local matrix that maps world position to [-1,1] unit volume
            // worldToLocal = Scale(1/halfExtents) * inverse(worldMatrix)
            glm::mat4 invWorld = glm::inverse(world.worldMatrix);
            glm::mat4 scaleToUnit = glm::scale(glm::mat4(1.0f),
                glm::vec3(1.0f / fog.halfExtents.x, 1.0f / fog.halfExtents.y, 1.0f / fog.halfExtents.z));
            gpuVol.worldToLocal = scaleToUnit * invWorld;

            // Compute world-space AABB from OBB
            glm::vec3 halfExt = fog.halfExtents;
            glm::vec3 corners[8] = {
                {-halfExt.x, -halfExt.y, -halfExt.z},
                { halfExt.x, -halfExt.y, -halfExt.z},
                {-halfExt.x,  halfExt.y, -halfExt.z},
                { halfExt.x,  halfExt.y, -halfExt.z},
                {-halfExt.x, -halfExt.y,  halfExt.z},
                { halfExt.x, -halfExt.y,  halfExt.z},
                {-halfExt.x,  halfExt.y,  halfExt.z},
                { halfExt.x,  halfExt.y,  halfExt.z}
            };

            glm::vec3 aabbMin(std::numeric_limits<float>::max());
            glm::vec3 aabbMax(std::numeric_limits<float>::lowest());

            for (const auto& corner : corners)
            {
                glm::vec3 worldCorner = glm::vec3(world.worldMatrix * glm::vec4(corner, 1.0f));
                aabbMin = glm::min(aabbMin, worldCorner);
                aabbMax = glm::max(aabbMax, worldCorner);
            }

            gpuVol.boundsMin = glm::vec4(aabbMin, 0.0f);
            gpuVol.boundsMax = glm::vec4(aabbMax, 0.0f);

            gpuVol.albedoAndDensity = glm::vec4(fog.albedo, fog.density);
            gpuVol.emissionAndFalloff = glm::vec4(fog.emission, fog.edgeFalloff);
            gpuVol.shapeType = static_cast<uint32_t>(fog.shape);
            gpuVol.blendMode = static_cast<uint32_t>(fog.blendMode);
            gpuVol.densityTextureIndex = -1;
            gpuVol.padding = 0;

            cpuVolumes.push_back(gpuVol);
            ++volumeCount;
        }
    }

    void FogVolumeBufferManager::uploadToGPU()
    {
        if (!initialized || !mappedMemory) return;

        // Write header (count + 3 padding)
        uint32_t header[4] = {volumeCount, 0, 0, 0};
        std::memcpy(mappedMemory, header, sizeof(header));

        // Write volume data
        if (volumeCount > 0)
        {
            auto* dst = static_cast<uint8_t*>(mappedMemory) + HEADER_SIZE;
            std::memcpy(dst, cpuVolumes.data(), volumeCount * sizeof(GPUFogVolume));
        }
    }
}

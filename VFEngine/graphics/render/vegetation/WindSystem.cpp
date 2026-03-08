#include "WindSystem.hpp"
#include "../../../utilities/vegetation/WindConfig.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include <cstring>

namespace render::vegetation
{
    WindSystem::~WindSystem()
    {
        cleanup();
    }

    void WindSystem::init(core::Device& device)
    {
        this->device = &device;

        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        core::BufferInfoRequest request(logicalDevice, physicalDevice);
        request.size = sizeof(GPUWindData);
        request.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        request.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                            vk::MemoryPropertyFlagBits::eHostCoherent;

        core::BufferUtilities::createBuffer(request, buffer, memory);
        mapped = logicalDevice.mapMemory(memory, 0, sizeof(GPUWindData), vk::MemoryMapFlags{});

        // Create descriptor set layout (single UBO binding)
        vk::DescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = vk::DescriptorType::eUniformBuffer;
        binding.descriptorCount = 1;
        binding.stageFlags = vk::ShaderStageFlagBits::eTaskEXT |
                             vk::ShaderStageFlagBits::eMeshEXT |
                             vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;
        descriptorSetLayout = logicalDevice.createDescriptorSetLayout(layoutInfo);

        // Create descriptor pool
        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eUniformBuffer;
        poolSize.descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        descriptorPool = logicalDevice.createDescriptorPool(poolInfo);

        // Allocate descriptor set
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;
        auto sets = logicalDevice.allocateDescriptorSets(allocInfo);
        descriptorSet = sets[0];

        // Write descriptor
        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(GPUWindData);

        vk::WriteDescriptorSet write{};
        write.dstSet = descriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eUniformBuffer;
        write.pBufferInfo = &bufferInfo;

        logicalDevice.updateDescriptorSets(write, {});

        // Initialize buffer with default (zeroed) wind data
        std::memcpy(mapped, &data, sizeof(GPUWindData));
    }

    void WindSystem::update(float deltaTime, const ::vegetation::WindConfig& config)
    {
        elapsedTime += deltaTime;

        glm::vec3 normalizedDir = glm::length(config.direction) > 0.0f
            ? glm::normalize(config.direction)
            : glm::vec3(1.0f, 0.0f, 0.0f);

        data.directionAndSpeed = glm::vec4(normalizedDir, config.speed);
        data.gustParams = glm::vec4(
            config.gustStrength,
            config.gustFrequency,
            config.turbulenceScale,
            elapsedTime
        );

        if (mapped)
        {
            std::memcpy(mapped, &data, sizeof(GPUWindData));
        }
    }

    void WindSystem::cleanup()
    {
        if (!device)
        {
            return;
        }

        const auto& logicalDevice = device->getLogicalDevice();

        if (mapped)
        {
            logicalDevice.unmapMemory(memory);
            mapped = nullptr;
        }

        if (descriptorPool)
        {
            logicalDevice.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }
        if (descriptorSetLayout)
        {
            logicalDevice.destroyDescriptorSetLayout(descriptorSetLayout);
            descriptorSetLayout = nullptr;
        }

        core::BufferUtilities::destroyBuffer(logicalDevice, buffer, memory);
        device = nullptr;
    }
}

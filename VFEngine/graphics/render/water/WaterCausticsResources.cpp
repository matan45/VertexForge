#include "WaterCausticsResources.hpp"
#include "../../core/Device.hpp"
#include "../../core/MemoryUtilities.hpp"

#include <cstring>

namespace render::water
{
    WaterCausticsResources::WaterCausticsResources(core::Device& device)
        : device(device)
    {
    }

    WaterCausticsResources::~WaterCausticsResources()
    {
        cleanup();
    }

    void WaterCausticsResources::init(vk::ImageView causticView)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Create sampler with repeat addressing for tiling caustics
        {
            vk::SamplerCreateInfo info{};
            info.magFilter = vk::Filter::eLinear;
            info.minFilter = vk::Filter::eLinear;
            info.addressModeU = vk::SamplerAddressMode::eRepeat;
            info.addressModeV = vk::SamplerAddressMode::eRepeat;
            info.addressModeW = vk::SamplerAddressMode::eRepeat;
            info.anisotropyEnable = VK_FALSE;
            info.maxAnisotropy = 1.0f;
            info.mipmapMode = vk::SamplerMipmapMode::eLinear;

            causticSampler = vkDevice.createSampler(info);
        }

        // Create params UBO (host-visible, persistently mapped)
        {
            vk::BufferCreateInfo bufferInfo{};
            bufferInfo.size = sizeof(CausticParams);
            bufferInfo.usage = vk::BufferUsageFlagBits::eUniformBuffer;
            paramsBuffer = vkDevice.createBuffer(bufferInfo);

            auto memReqs = vkDevice.getBufferMemoryRequirements(paramsBuffer);
            vk::MemoryAllocateInfo allocInfo{};
            allocInfo.allocationSize = memReqs.size;
            allocInfo.memoryTypeIndex = core::MemoryUtilities::findMemoryType(
                device.getPhysicalDevice(), memReqs.memoryTypeBits,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            paramsMemory = vkDevice.allocateMemory(allocInfo);
            vkDevice.bindBufferMemory(paramsBuffer, paramsMemory, 0);
            paramsMapped = vkDevice.mapMemory(paramsMemory, 0, sizeof(CausticParams));
        }

        // Create descriptor set layout (2 bindings, fragment stage)
        {
            std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
            bindings[0].binding = 0;
            bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

            bindings[1].binding = 1;
            bindings[1].descriptorType = vk::DescriptorType::eUniformBuffer;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();
            descriptorSetLayout = vkDevice.createDescriptorSetLayout(layoutInfo);
        }

        // Create descriptor pool (1 combined image sampler + 1 uniform buffer, 1 set)
        {
            std::array<vk::DescriptorPoolSize, 2> poolSizes{};
            poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
            poolSizes[0].descriptorCount = 1;
            poolSizes[1].type = vk::DescriptorType::eUniformBuffer;
            poolSizes[1].descriptorCount = 1;

            vk::DescriptorPoolCreateInfo poolInfo{};
            poolInfo.maxSets = 1;
            poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
            poolInfo.pPoolSizes = poolSizes.data();

            descriptorPool = vkDevice.createDescriptorPool(poolInfo);
        }

        // Allocate descriptor set
        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &descriptorSetLayout;
            descriptorSet = vkDevice.allocateDescriptorSets(allocInfo)[0];
        }

        // Write descriptors
        {
            vk::DescriptorImageInfo imageInfo{};
            imageInfo.sampler = causticSampler;
            imageInfo.imageView = causticView;
            imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

            vk::DescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = paramsBuffer;
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(CausticParams);

            std::array<vk::WriteDescriptorSet, 2> writes{};
            writes[0].dstSet = descriptorSet;
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writes[0].pImageInfo = &imageInfo;

            writes[1].dstSet = descriptorSet;
            writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = vk::DescriptorType::eUniformBuffer;
            writes[1].pBufferInfo = &bufferInfo;

            vkDevice.updateDescriptorSets(writes, nullptr);
        }

        initialized = true;
    }

    void WaterCausticsResources::updateParams(const CausticParams& params)
    {
        if (paramsMapped)
            std::memcpy(paramsMapped, &params, sizeof(CausticParams));
    }

    void WaterCausticsResources::cleanup()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        if (descriptorPool)
        {
            vkDevice.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }

        if (descriptorSetLayout) { vkDevice.destroyDescriptorSetLayout(descriptorSetLayout); descriptorSetLayout = nullptr; }
        if (causticSampler)      { vkDevice.destroySampler(causticSampler); causticSampler = nullptr; }

        if (paramsMapped)
        {
            vkDevice.unmapMemory(paramsMemory);
            paramsMapped = nullptr;
        }
        if (paramsBuffer) { vkDevice.destroyBuffer(paramsBuffer); paramsBuffer = nullptr; }
        if (paramsMemory) { vkDevice.freeMemory(paramsMemory); paramsMemory = nullptr; }

        initialized = false;
    }
}

#include "BindlessTextureManager.hpp"
#include "../../core/Device.hpp"
#include "print/Log.hpp"
#include <stdexcept>


namespace render::gpudriven {

    BindlessTextureManager::BindlessTextureManager(core::Device& device)
        : device(device)
    {
    }

    BindlessTextureManager::~BindlessTextureManager()
    {
        cleanup();
    }

    void BindlessTextureManager::init()
    {
        if (initialized) {
            return;
        }

        vfLogInfo("BindlessTextureManager: Initializing with max {} textures", MAX_BINDLESS_TEXTURES);

        createDescriptorSetLayout();
        createDescriptorPool();
        allocateDescriptorSet();

        initialized = true;
    }

    void BindlessTextureManager::cleanup()
    {
        if (!initialized) {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();

        // Descriptor set is freed when pool is destroyed
        if (descriptorPool) {
            vkDevice.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }

        if (descriptorSetLayout) {
            vkDevice.destroyDescriptorSetLayout(descriptorSetLayout);
            descriptorSetLayout = nullptr;
        }

        texturePathToIndex.clear();
        nextTextureIndex = 1;
        initialized = false;
        defaultTextureSet = false;

    }

    void BindlessTextureManager::createDescriptorSetLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Binding for the bindless texture array
        vk::DescriptorSetLayoutBinding textureBinding{};
        textureBinding.binding = 0;
        textureBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        textureBinding.descriptorCount = MAX_BINDLESS_TEXTURES;
        textureBinding.stageFlags = vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute;
        textureBinding.pImmutableSamplers = nullptr;

        // Binding flags for descriptor indexing
        std::array<vk::DescriptorBindingFlags, 1> bindingFlags = {
            vk::DescriptorBindingFlagBits::ePartiallyBound |
            vk::DescriptorBindingFlagBits::eVariableDescriptorCount |
            vk::DescriptorBindingFlagBits::eUpdateAfterBind
        };

        vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
        bindingFlagsInfo.bindingCount = 1;
        bindingFlagsInfo.pBindingFlags = bindingFlags.data();

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.pNext = &bindingFlagsInfo;
        layoutInfo.flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &textureBinding;

        descriptorSetLayout = vkDevice.createDescriptorSetLayout(layoutInfo);
        vfLogWarning("BindlessTextureManager: Created descriptor set layout");
    }

    void BindlessTextureManager::createDescriptorPool()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eCombinedImageSampler;
        poolSize.descriptorCount = MAX_BINDLESS_TEXTURES;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        descriptorPool = vkDevice.createDescriptorPool(poolInfo);
        vfLogWarning("BindlessTextureManager: Created descriptor pool");
    }

    void BindlessTextureManager::allocateDescriptorSet()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Variable descriptor count for the texture array
        uint32_t variableDescCount = MAX_BINDLESS_TEXTURES;

        vk::DescriptorSetVariableDescriptorCountAllocateInfo variableCountInfo{};
        variableCountInfo.descriptorSetCount = 1;
        variableCountInfo.pDescriptorCounts = &variableDescCount;

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.pNext = &variableCountInfo;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        std::vector<vk::DescriptorSet> sets = vkDevice.allocateDescriptorSets(allocInfo);
        descriptorSet = sets[0];

        vfLogWarning("BindlessTextureManager: Allocated descriptor set");
    }

    void BindlessTextureManager::setDefaultTexture(vk::ImageView imageView, vk::Sampler sampler)
    {
        if (!initialized) {
            throw std::runtime_error("BindlessTextureManager: Cannot set default texture before initialization");
        }

        // Default texture is always at index 0
        updateDescriptor(0, imageView, sampler);
        defaultTextureSet = true;

        vfLogWarning("BindlessTextureManager: Set default texture at index 0");
    }

    uint32_t BindlessTextureManager::registerTexture(const std::string& path, vk::ImageView imageView, vk::Sampler sampler)
    {
        if (!initialized) {
            throw std::runtime_error("BindlessTextureManager: Cannot register texture before initialization");
        }
        
        auto it = texturePathToIndex.find(path);
        if (it != texturePathToIndex.end()) {
            return it->second;
        }

        // Check capacity
        if (nextTextureIndex >= MAX_BINDLESS_TEXTURES) {
            vfLogError("BindlessTextureManager: Maximum texture count ({}) exceeded", MAX_BINDLESS_TEXTURES);
            return INVALID_TEXTURE_INDEX;
        }
        
        uint32_t index = nextTextureIndex++;
        texturePathToIndex[path] = index;
        
        updateDescriptor(index, imageView, sampler);

        vfLogWarning("BindlessTextureManager: Registered texture '{}' at index {}", path, index);
        return index;
    }

    uint32_t BindlessTextureManager::getTextureIndex(const std::string& path) const
    {
        auto it = texturePathToIndex.find(path);
        if (it != texturePathToIndex.end()) {
            return it->second;
        }
        return INVALID_TEXTURE_INDEX;
    }

    void BindlessTextureManager::updateDescriptor(uint32_t index, vk::ImageView imageView, vk::Sampler sampler)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfo.imageView = imageView;
        imageInfo.sampler = sampler;

        vk::WriteDescriptorSet descriptorWrite{};
        descriptorWrite.dstSet = descriptorSet;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = index;
        descriptorWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        vkDevice.updateDescriptorSets(1, &descriptorWrite, 0, nullptr);
    }

}

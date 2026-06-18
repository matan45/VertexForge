#include "RTShadowMaskSet.hpp"
#include "../../core/Device.hpp"
#include <array>

namespace render::raytracing
{
    RTShadowMaskSet::RTShadowMaskSet(core::Device& device)
        : device(device)
    {
    }

    RTShadowMaskSet::~RTShadowMaskSet()
    {
        cleanup();
    }

    void RTShadowMaskSet::init()
    {
        if (initialized) return;

        vk::Device vkDevice = device.getLogicalDevice();

        // Three combined-image-sampler bindings (directional / spot / point), fragment stage.
        std::array<vk::DescriptorSetLayoutBinding, 3> bindings{};
        for (uint32_t i = 0; i < bindings.size(); ++i)
            bindings[i] = {i, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment};

        // PARTIALLY_BOUND so a binding may stay unwritten while its RT type is inactive; the shader
        // only statically uses a binding when its RT_*_ENABLED macro is set (and it is then written).
        std::array<vk::DescriptorBindingFlags, 3> bindingFlags{};
        bindingFlags.fill(vk::DescriptorBindingFlagBits::ePartiallyBound);

        vk::DescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
        flagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
        flagsInfo.pBindingFlags = bindingFlags.data();

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        layoutInfo.pNext = &flagsInfo;
        layout = vkDevice.createDescriptorSetLayout(layoutInfo);

        // Ring one set per swapchain image (VK-1398). Pool sized for 3 bindings x image count.
        vk::DescriptorPoolSize poolSize{vk::DescriptorType::eCombinedImageSampler,
                                        3 * core::MAX_SWAPCHAIN_IMAGES};
        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = core::MAX_SWAPCHAIN_IMAGES;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        pool = vkDevice.createDescriptorPool(poolInfo);

        for (uint32_t i = 0; i < core::MAX_SWAPCHAIN_IMAGES; ++i)
        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = pool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &layout;
            descriptorSets[i] = vkDevice.allocateDescriptorSets(allocInfo)[0];
        }

        initialized = true;
    }

    void RTShadowMaskSet::cleanup()
    {
        if (!initialized) return;
        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.destroyDescriptorPool(pool);
        vkDevice.destroyDescriptorSetLayout(layout);
        pool = nullptr;
        layout = nullptr;
        descriptorSets.fill(nullptr);
        initialized = false;
    }

    void RTShadowMaskSet::copyInto(uint32_t dstBinding, vk::DescriptorSet srcSet, uint32_t imageIndex)
    {
        if (!initialized || !srcSet || imageIndex >= core::MAX_SWAPCHAIN_IMAGES) return;

        vk::CopyDescriptorSet copy{};
        copy.srcSet = srcSet;
        copy.srcBinding = 0;
        copy.srcArrayElement = 0;
        copy.dstSet = descriptorSets[imageIndex];
        copy.dstBinding = dstBinding;
        copy.dstArrayElement = 0;
        copy.descriptorCount = 1;
        device.getLogicalDevice().updateDescriptorSets(0, nullptr, 1, &copy);
    }
}

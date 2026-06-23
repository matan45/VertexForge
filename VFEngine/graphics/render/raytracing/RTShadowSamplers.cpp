#include "RTShadowSamplers.hpp"

namespace render::raytracing
{
    vk::Sampler createGuideSampler(const vk::Device& device)
    {
        vk::SamplerCreateInfo nearestInfo{};
        nearestInfo.magFilter = vk::Filter::eNearest;
        nearestInfo.minFilter = vk::Filter::eNearest;
        nearestInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        nearestInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        nearestInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        return device.createSampler(nearestInfo);
    }

    vk::Sampler createDenoisedMaskSampler(const vk::Device& device)
    {
        // Linear filter, clampToEdge — matches the guide sampler's address modes, bumps the filter.
        vk::SamplerCreateInfo linearInfo{};
        linearInfo.magFilter = vk::Filter::eLinear;
        linearInfo.minFilter = vk::Filter::eLinear;
        linearInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        linearInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        linearInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        return device.createSampler(linearInfo);
    }

    vk::DescriptorSetLayout createDenoisedMaskLayout(const vk::Device& device)
    {
        vk::DescriptorSetLayoutBinding maskBinding{0, vk::DescriptorType::eCombinedImageSampler, 1,
                                                   vk::ShaderStageFlagBits::eFragment};
        vk::DescriptorSetLayoutCreateInfo maskLayoutInfo{};
        maskLayoutInfo.bindingCount = 1;
        maskLayoutInfo.pBindings = &maskBinding;
        return device.createDescriptorSetLayout(maskLayoutInfo);
    }
}

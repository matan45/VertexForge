#include "ShadowSamplers.hpp"

namespace render::shadow
{
    vk::SamplerCreateInfo ShadowSamplers::createBaseSamplerInfo()
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToBorder;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToBorder;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToBorder;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = vk::CompareOp::eNever;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;
        samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        return samplerInfo;
    }

    vk::Sampler ShadowSamplers::createComparisonSampler(vk::Device device)
    {
        auto samplerInfo = createBaseSamplerInfo();
        samplerInfo.compareEnable = VK_TRUE;
        samplerInfo.compareOp = vk::CompareOp::eLessOrEqual;
        return device.createSampler(samplerInfo);
    }

    vk::Sampler ShadowSamplers::createCubeComparisonSampler(vk::Device device)
    {
        auto samplerInfo = createBaseSamplerInfo();
        samplerInfo.compareEnable = VK_TRUE;
        samplerInfo.compareOp = vk::CompareOp::eLessOrEqual;
        return device.createSampler(samplerInfo);
    }

    vk::Sampler ShadowSamplers::createDepthSampler(vk::Device device)
    {
        auto samplerInfo = createBaseSamplerInfo();
        samplerInfo.magFilter = vk::Filter::eNearest;
        samplerInfo.minFilter = vk::Filter::eNearest;
        return device.createSampler(samplerInfo);
    }

    vk::Sampler ShadowSamplers::createCubeDepthSampler(vk::Device device)
    {
        auto samplerInfo = createBaseSamplerInfo();
        samplerInfo.magFilter = vk::Filter::eNearest;
        samplerInfo.minFilter = vk::Filter::eNearest;
        return device.createSampler(samplerInfo);
    }
}

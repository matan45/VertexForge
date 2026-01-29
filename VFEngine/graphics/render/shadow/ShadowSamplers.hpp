#pragma once

#include <vulkan/vulkan.hpp>

namespace render::shadow
{
    class ShadowSamplers
    {
    public:
        static vk::Sampler createComparisonSampler(vk::Device device);
        static vk::Sampler createCubeComparisonSampler(vk::Device device);

    private:
        static vk::SamplerCreateInfo createBaseSamplerInfo();
    };
}

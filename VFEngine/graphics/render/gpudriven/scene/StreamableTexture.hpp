#pragma once

#include "../../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <string>
#include <cstdint>

namespace render::gpudriven
{
    struct StreamableTexture
    {
        vk::Image image;
        core::VulkanAllocation allocation;
        vk::ImageView view;
        vk::Sampler currentSampler;
        uint32_t totalMipLevels = 0;
        uint32_t lowestLoadedMip = 0;   // 0 = full res loaded
        uint32_t bindlessIndex = 0;
        vk::Format format = vk::Format::eR8G8B8A8Unorm;
        uint32_t width = 0;
        uint32_t height = 0;
        size_t gpuMemoryUsage = 0;
        std::string path;
        uint64_t lastAccessFrame = 0;
        float distanceToCamera = 0.0f;
    };
}

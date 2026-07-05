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
        // VK-1480: a TailOnly image holds only the coarse tail mips (base <= 128px) as a permanent,
        // tiny fallback for an SVT-paged texture. Its mips are all resident at creation and it is
        // excluded from the streaming/eviction machinery. promoteToFull() converts it back to a full
        // streamed image in place (same bindless slot) when SVT is disabled.
        bool tailOnly = false;
    };
}

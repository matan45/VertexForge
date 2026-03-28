#pragma once

#include <vulkan/vulkan.hpp>
#include "../../core/VulkanMemoryManager.hpp"
#include <glm/glm.hpp>

namespace vegetation
{
    struct WindConfig;
}

namespace core
{
    class Device;
}

namespace render::vegetation
{
    struct GPUWindData
    {
        glm::vec4 directionAndSpeed;    // xyz=direction, w=speed
        glm::vec4 gustParams;           // x=strength, y=frequency, z=turbulenceScale, w=time
    };

    class WindSystem
    {
    private:
        core::Device* device = nullptr;

        vk::Buffer buffer;
        core::VulkanAllocation allocation;
        void* mapped = nullptr;
        GPUWindData data{};
        float elapsedTime = 0.0f;

        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

    public:
        WindSystem() = default;
        ~WindSystem();

        WindSystem(const WindSystem&) = delete;
        WindSystem& operator=(const WindSystem&) = delete;

        void init(core::Device& device);
        void update(float deltaTime, const ::vegetation::WindConfig& config);
        void cleanup();

        vk::Buffer getBuffer() const { return buffer; }
        const GPUWindData& getData() const { return data; }
        vk::DescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
        vk::DescriptorSet getDescriptorSet() const { return descriptorSet; }
    };
}

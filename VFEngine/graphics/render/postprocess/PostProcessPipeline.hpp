#pragma once

#include "PostProcessEffect.hpp"
#include <memory>
#include <vector>

namespace core
{
    class Device;
    class SwapChain;
    struct OffscreenResources;
}

namespace render::postprocess
{
    struct PingPongTarget
    {
        vk::Image image;
        vk::DeviceMemory memory;
        vk::ImageView imageView;
        vk::Framebuffer framebuffer;
    };

    class PostProcessPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;

        bool initialized = false;

        // Shared resources
        vk::RenderPass renderPass;
        vk::Sampler linearSampler;
        vk::DescriptorSetLayout inputDescriptorSetLayout;
        vk::DescriptorPool descriptorPool;

        // Ping-pong targets
        PingPongTarget targetA{};
        PingPongTarget targetB{};

        // Descriptor sets for sampling each source
        vk::DescriptorSet descriptorSetA; // samples targetA
        vk::DescriptorSet descriptorSetB; // samples targetB
        std::vector<vk::DescriptorSet> sceneDescriptorSets; // samples scene color per swapchain image

        // Effect chain (sorted by priority)
        std::vector<std::unique_ptr<PostProcessEffect>> effects;

    public:
        explicit PostProcessPipeline(core::Device& device, core::SwapChain& swapChain,
                                     core::OffscreenResources& offscreenResources);
        ~PostProcessPipeline();

        void execute(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex);

        void recreate();
        void cleanup();

        void addEffect(std::unique_ptr<PostProcessEffect> effect);
        void removeEffect(::postprocess::EffectType type);
        void updateSettings(const ::postprocess::PostProcessSettings& settings);
        void applySettings(const ::postprocess::PostProcessSettings& settings);

        bool hasEnabledEffects() const;
        bool isInitialized() const { return initialized; }

        vk::RenderPass getRenderPass() const { return renderPass; }
        vk::DescriptorSetLayout getInputDescriptorSetLayout() const { return inputDescriptorSetLayout; }
        vk::Sampler getLinearSampler() const { return linearSampler; }

    private:
        void lazyInit();
        void createRenderPass();
        void createSampler();
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void createPingPongTargets();
        void createFramebuffers();
        void createDescriptorSets();
        void updateDescriptorSet(vk::DescriptorSet set, vk::ImageView imageView);

        void cleanupPingPongTargets();
        void sortEffects();
    };
}

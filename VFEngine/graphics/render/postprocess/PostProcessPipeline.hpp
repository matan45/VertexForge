#pragma once

#include "PostProcessEffect.hpp"
#include <glm/glm.hpp>
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
    struct SunInfo
    {
        glm::vec2 screenPos{0.5f};
        bool hasSun = false;
    };

    struct CameraInfo
    {
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        glm::vec3 cameraPosition{0.0f};
        glm::mat4 viewMatrix{1.0f};
        float time = 0.0f;
    };

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

        // Sun data for effects that need it (God Rays)
        SunInfo sunInfo{};

        // Camera data for effects that need it (Depth of Field)
        CameraInfo cameraInfo{};

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

        void setSunData(const glm::vec2& screenPos, bool hasSun);
        const SunInfo& getSunData() const { return sunInfo; }

        void setCameraData(float nearPlane, float farPlane, const glm::vec3& cameraPosition,
                          const glm::mat4& viewMatrix, float time);
        const CameraInfo& getCameraData() const { return cameraInfo; }

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

#pragma once
#include "../core/OffScreen.hpp"
#include <rendertexture/RenderTextureTypes.hpp>
#include <glm/glm.hpp>
#include <memory>

namespace core
{
    class Device;
    class SwapChain;
    class CommandPool;
}

namespace render
{
    class RenderPassHandler;

    namespace gpudriven
    {
        class GPUDrivenRenderer;
    }

    class RenderTextureViewPort
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        std::unique_ptr<core::CommandPool> commandPool;

        vk::Sampler sampler;
        core::OffscreenResources offscreenResources;
        std::vector<vk::Fence> inFlightFences;

        uint32_t width = 512;
        uint32_t height = 512;
        glm::vec4 clearColor{0.0f, 0.0f, 0.0f, 1.0f};
        uint32_t lastRenderedImageIndex = 0;
        bool initialized = false;

    public:
        RenderTextureViewPort(core::Device& device, core::SwapChain& swapChain);
        ~RenderTextureViewPort();

        RenderTextureViewPort(const RenderTextureViewPort&) = delete;
        RenderTextureViewPort& operator=(const RenderTextureViewPort&) = delete;

        void init(uint32_t width, uint32_t height);
        void resize(uint32_t newWidth, uint32_t newHeight);
        void cleanUp();

        void setClearColor(const glm::vec4& color) { clearColor = color; }

        // Render scene from RTT camera perspective using the shared main renderer
        vk::DescriptorSet render(
            RenderPassHandler* mainPassHandler,
            const glm::mat4& view,
            const glm::mat4& projection,
            const glm::vec3& cameraPosition,
            float nearPlane,
            float farPlane
        );

        uint32_t getWidth() const { return width; }
        uint32_t getHeight() const { return height; }
        bool isInitialized() const { return initialized; }

        vk::ImageView getLastRenderedImageView() const;
        vk::Image getLastRenderedImage() const;
        vk::Sampler getTextureSampler() const { return sampler; }

    private:
        void createOffscreenResources();
        void cleanupOffscreenResources();
        void createSampler();
        void updateDescriptorSets(vk::DescriptorSet& descriptorSet, const vk::ImageView& imageView) const;
    };
}

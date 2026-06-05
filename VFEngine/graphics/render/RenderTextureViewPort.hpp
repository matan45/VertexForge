#pragma once
#include "../core/OffScreen.hpp"
#include "../core/VulkanMemoryManager.hpp"
#include <rendertexture/RenderTextureTypes.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

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
        class GPUDrivenCameraBuffer;
    }

    class RenderTextureViewPort
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        std::unique_ptr<core::CommandPool> commandPool;

        vk::Sampler sampler;
        core::OffscreenResources offscreenResources;
        std::vector<core::DepthImage> depthImages;
        std::vector<vk::Fence> inFlightFences;

        uint32_t width = 512;
        uint32_t height = 512;
        glm::vec4 clearColor{0.0f, 0.0f, 0.0f, 1.0f};
        uint32_t lastRenderedImageIndex = 0;
        bool initialized = false;

        // VK-1334: per-RTT camera resources. Lazily allocated on the first render() call once
        // the main mesh / cull pipelines are reachable through `mainPassHandler`. RTT recording
        // writes only into these buffers and binds only these descriptor sets, so the shared
        // main-camera buffers are never CPU-mutated mid-flight.
        bool perRTTResourcesInitialized = false;

        // Per-image static-mesh CameraUBO (binds at set 0 / binding 0 of the IBL descriptor set).
        std::vector<vk::Buffer> rttMeshCameraUBOs;
        std::vector<core::VulkanAllocation> rttMeshCameraUBOAllocs;
        vk::DescriptorPool rttMeshIBLDescPool;
        std::vector<vk::DescriptorSet> rttMeshIBLDescSets;

        // Per-image GPU-driven cull camera buffer (binds at set 0 / binding 1 of the cull set).
        std::vector<std::unique_ptr<gpudriven::GPUDrivenCameraBuffer>> rttGPUDrivenCameraBuffers;
        vk::DescriptorPool rttCullDescPool;
        std::vector<vk::DescriptorSet> rttCullDescSets;

        gpudriven::GPUDrivenRenderer* rttCullDescPoolOwnerRenderer = nullptr;

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
        uint32_t getLastRenderedImageIndex() const { return lastRenderedImageIndex; }
        uint32_t getImageCount() const { return static_cast<uint32_t>(offscreenResources.colorImages.size()); }
        vk::ImageView getImageView(uint32_t imageIndex) const;
        vk::Sampler getTextureSampler() const { return sampler; }

        // VK-1334 minimap flicker fix: returns the view for the slot most recently rendered.
        // Cold slots are pre-cleared to clearColor at create/resize time, so this is always a
        // safe-to-sample image even before any render() call.
        vk::ImageView getLatestImageView() const { return getImageView(lastRenderedImageIndex); }

    private:
        void createOffscreenResources();
        void cleanupOffscreenResources();
        void createSampler();
        void updateDescriptorSets(vk::DescriptorSet& descriptorSet, const vk::ImageView& imageView) const;

        // VK-1334
        void ensurePerRTTResources(RenderPassHandler* mainPassHandler);
        void cleanupPerRTTResources();
    };
}

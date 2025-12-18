#pragma once

#include "BillboardTypes.hpp"
#include <memory>
#include <vector>
#include <string>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
    class Texture;
    struct OffscreenResources;
}

namespace render::billboard
{
    class BillboardPipeline
    {
    public:
        explicit BillboardPipeline(core::Device& device, core::SwapChain& swapChain,
                                   core::OffscreenResources& offscreenResources);
        ~BillboardPipeline();

        void init();
        void recreate();
        void cleanUp();

        // Load atlas texture from file
        bool loadAtlas(const std::string& atlasPath);

        // Update camera UBO
        void updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                            const glm::vec3& cameraPos) const;

        // Set billboards to render this frame
        void setBillboardList(const std::vector<BillboardRenderData>& billboards);

        // Record rendering commands
        void recordCommandBuffer(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;

        // Check if pipeline is ready
        bool isInitialized() const { return initialized; }

        // Getters
        vk::RenderPass getRenderPass() const { return renderPass; }
        vk::Pipeline getGraphicsPipeline() const { return graphicsPipeline; }

    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;

        bool initialized = false;
        bool atlasLoaded = false;

        // Shader
        std::shared_ptr<core::Shader> billboardShader;

        // Vulkan resources
        vk::RenderPass renderPass;
        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        std::vector<vk::Framebuffer> framebuffers;

        // Camera UBO
        vk::Buffer cameraUBO;
        vk::DeviceMemory cameraUBOMemory;

        // Quad vertex buffer (static, created once)
        vk::Buffer quadVertexBuffer;
        vk::DeviceMemory quadVertexBufferMemory;
        vk::Buffer quadIndexBuffer;
        vk::DeviceMemory quadIndexBufferMemory;

        // Instance buffer (dynamic, updated each frame)
        vk::Buffer instanceBuffer;
        vk::DeviceMemory instanceBufferMemory;
        uint32_t maxInstances = 1024;
        uint32_t currentInstanceCount = 0;

        // Atlas texture (using core::Texture for file loading)
        std::unique_ptr<core::Texture> atlasTexture;

        // Default atlas (procedural fallback)
        vk::Image defaultAtlasImage;
        vk::DeviceMemory defaultAtlasImageMemory;
        vk::ImageView defaultAtlasImageView;
        vk::Sampler defaultAtlasSampler;

        // Billboard list for current frame
        std::vector<BillboardRenderData> currentBillboards;

        // Private methods
        void loadShader();
        void createRenderPass();
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void createDescriptorSet();
        void createCameraUBO();
        void createPipelineLayout();
        void createGraphicsPipeline();
        void createFramebuffers();
        void createQuadBuffers();
        void createInstanceBuffer();
        void createDefaultAtlas();

        void updateInstanceBuffer();
    };
}

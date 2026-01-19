#pragma once

#include "VFXBillboardTypes.hpp"
#include <memory>
#include <vector>
#include <string>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
}

namespace render::vfx
{
    // VFX pipeline variant designed for scene-integrated rendering.
    // Unlike VFXBillboardPipeline (which creates its own render pass for offscreen preview),
    // this pipeline uses an external render pass provided by the scene rendering system.
    class VFXScenePipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        bool initialized = false;

        std::shared_ptr<core::Shader> vfxShader;

        vk::RenderPass externalRenderPass;  // Not owned - provided by scene
        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        // Buffers
        vk::Buffer quadVertexBuffer;
        vk::DeviceMemory quadVertexBufferMemory;
        vk::Buffer quadIndexBuffer;
        vk::DeviceMemory quadIndexBufferMemory;
        vk::Buffer instanceBuffer;
        vk::DeviceMemory instanceBufferMemory;
        vk::Buffer cameraUBO;
        vk::DeviceMemory cameraUBOMemory;

        uint32_t maxInstances = 4096;  // Higher limit for multiple scene VFX emitters
        uint32_t currentInstanceCount = 0;

        // Default texture (white 1x1)
        vk::Image defaultTextureImage;
        vk::DeviceMemory defaultTextureMemory;
        vk::ImageView defaultTextureImageView;
        vk::Sampler textureSampler;

    public:
        explicit VFXScenePipeline(core::Device& device, core::SwapChain& swapChain);
        ~VFXScenePipeline();

        // Initialize with external render pass (scene's render pass)
        void init(vk::RenderPass renderPass);
        void recreate(vk::RenderPass renderPass);
        void cleanUp();

        void updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                             const glm::vec3& cameraPos, float time) const;

        void setParticleInstances(const std::vector<VFXInstanceData>& instances);

        // Record draw commands inline within existing render pass (no begin/end)
        void recordCommandsInline(const vk::CommandBuffer& commandBuffer) const;

        bool isInitialized() const { return initialized; }

        uint32_t getInstanceCount() const { return currentInstanceCount; }

    private:
        void loadShader();
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void createDescriptorSet();
        void createPipeline();
        void createBuffers();
        void createDefaultTexture();
        void createSampler();

        void updateDescriptorSet();
    };
}

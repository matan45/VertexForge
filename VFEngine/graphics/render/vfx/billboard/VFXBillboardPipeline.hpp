#pragma once

#include "VFXBillboardTypes.hpp"
#include "../compute/GPUVFXTypes.hpp"
#include "../../../core/VulkanMemoryManager.hpp"
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

namespace render::vfx
{
    class VFXBillboardPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;

        bool initialized = false;

        std::shared_ptr<core::Shader> vfxShader;

        vk::RenderPass renderPass;
        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        std::vector<vk::Framebuffer> framebuffers;

        vk::Buffer quadVertexBuffer;
        core::VulkanAllocation quadVertexBufferAllocation;
        vk::Buffer quadIndexBuffer;
        core::VulkanAllocation quadIndexBufferAllocation;
        vk::Buffer instanceBuffer;
        core::VulkanAllocation instanceBufferAllocation;
        vk::Buffer cameraUBO;
        core::VulkanAllocation cameraUBOAllocation;

        uint32_t maxInstances = 1024;
        uint32_t currentInstanceCount = 0;

        vk::Image defaultTextureImage;
        core::VulkanAllocation defaultTextureAllocation;
        vk::ImageView defaultTextureImageView;
        vk::Sampler textureSampler;

        std::unique_ptr<core::Texture> customTexture;
        std::string currentTexturePath;

        VFXFlipbookPushConstants flipbookPC{1.0f, 1.0f, 0.1f, 0, RenderModeFlags::Billboard, 1.0f};

    public:
        explicit VFXBillboardPipeline(core::Device& device, core::SwapChain& swapChain,
                                      core::OffscreenResources& offscreenResources);
        ~VFXBillboardPipeline();

        void init();
        void recreate();
        void cleanUp();

        void updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                             const glm::vec3& cameraPos, float time) const;

        void setParticleInstances(const std::vector<VFXInstanceData>& instances);

        void setTexture(const std::string& texturePath);

        void setFlipbookConfig(const VFXFlipbookConfig& config);

        void recordCommandBuffer(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;

        bool isInitialized() const { return initialized; }

    private:
        void loadShader();
        void createRenderPass();
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void createDescriptorSet();
        void createPipeline();
        void createFramebuffers();
        void createBuffers();
        void createDefaultTexture();
        void createSampler();

        void updateDescriptorSet();
    };
}

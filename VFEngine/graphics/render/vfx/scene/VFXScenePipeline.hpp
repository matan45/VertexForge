#pragma once

#include "../billboard/VFXBillboardTypes.hpp"
#include "vfx/VFXBlendMode.hpp"
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
}

namespace render::vfx
{
    class VFXScenePipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        bool initialized = false;

        std::shared_ptr<core::Shader> vfxShader;

        vk::Format colorFormat = vk::Format::eUndefined;
        vk::Format depthFormat = vk::Format::eUndefined;
        vk::Pipeline graphicsPipeline;
        vk::Pipeline multiplyPipeline; // VK-1472: Multiply blend variant (shares pipelineLayout)
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        vk::Buffer quadVertexBuffer;
        core::VulkanAllocation quadVertexBufferAllocation;
        vk::Buffer quadIndexBuffer;
        core::VulkanAllocation quadIndexBufferAllocation;
        vk::Buffer instanceBuffer;
        core::VulkanAllocation instanceBufferAllocation;
        vk::Buffer cameraUBO;
        core::VulkanAllocation cameraUBOAllocation;

        uint32_t maxInstances = 4096;
        uint32_t currentInstanceCount = 0;

        vk::Image defaultTextureImage;
        core::VulkanAllocation defaultTextureAllocation;
        vk::ImageView defaultTextureImageView;
        vk::Sampler textureSampler;

        std::unique_ptr<core::Texture> customTexture;
        std::string currentTexturePath;

        VFXFlipbookPushConstants flipbookPC{1.0f, 1.0f, 0.1f, 0, RenderModeFlags::Billboard, 1.0f};

    public:
        explicit VFXScenePipeline(core::Device& device, core::SwapChain& swapChain);
        ~VFXScenePipeline();

        void init(vk::Format colorFormat, vk::Format depthFormat);
        void recreate(vk::Format colorFormat, vk::Format depthFormat);
        void cleanUp();

        void updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                             const glm::vec3& cameraPos, float time) const;

        void setParticleInstances(const std::vector<VFXInstanceData>& instances);

        void setTexture(const std::string& texturePath);

        void setFlipbookConfig(const VFXFlipbookConfig& config);

        void recordCommandsInline(const vk::CommandBuffer& commandBuffer) const;

        bool isInitialized() const { return initialized; }

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

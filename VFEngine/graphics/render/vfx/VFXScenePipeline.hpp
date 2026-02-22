#pragma once

#include "VFXBillboardTypes.hpp"
#include "GPUVFXTypes.hpp"
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

        vk::RenderPass externalRenderPass;
        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        vk::Buffer quadVertexBuffer;
        vk::DeviceMemory quadVertexBufferMemory;
        vk::Buffer quadIndexBuffer;
        vk::DeviceMemory quadIndexBufferMemory;
        vk::Buffer instanceBuffer;
        vk::DeviceMemory instanceBufferMemory;
        vk::Buffer cameraUBO;
        vk::DeviceMemory cameraUBOMemory;

        uint32_t maxInstances = 4096;
        uint32_t currentInstanceCount = 0;

        vk::Image defaultTextureImage;
        vk::DeviceMemory defaultTextureMemory;
        vk::ImageView defaultTextureImageView;
        vk::Sampler textureSampler;

        std::unique_ptr<core::Texture> customTexture;
        std::string currentTexturePath;

        VFXFlipbookPushConstants flipbookPC{1.0f, 1.0f};

    public:
        explicit VFXScenePipeline(core::Device& device, core::SwapChain& swapChain);
        ~VFXScenePipeline();

        void init(vk::RenderPass renderPass);
        void recreate(vk::RenderPass renderPass);
        void cleanUp();

        void updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                             const glm::vec3& cameraPos, float time) const;

        void setParticleInstances(const std::vector<VFXInstanceData>& instances);

        void setTexture(const std::string& texturePath);

        void setFlipbookConfig(int rows, int columns);

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

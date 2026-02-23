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
    class Texture;
    struct OffscreenResources;
}

namespace render::vfx
{
    // Push constants for ribbon preview shader
    struct VFXRibbonPreviewPushConstants
    {
        float alphaClipThreshold = 0.1f;
        uint32_t blendMode = 0;
        float ribbonWidth = 1.0f;
        float glowColorR = 1.0f;
        float glowColorG = 1.0f;
        float glowColorB = 1.0f;
        float uvScrollSpeedU = 0.0f;
        float uvScrollSpeedV = 0.0f;
    };

    class VFXRibbonPreviewPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;

        bool initialized = false;

        std::shared_ptr<core::Shader> ribbonShader;

        vk::RenderPass renderPass;
        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        std::vector<vk::Framebuffer> framebuffers;

        vk::Buffer quadVertexBuffer;
        vk::DeviceMemory quadVertexBufferMemory;
        vk::Buffer quadIndexBuffer;
        vk::DeviceMemory quadIndexBufferMemory;
        vk::Buffer instanceBuffer;
        vk::DeviceMemory instanceBufferMemory;
        void* instanceBufferMapped = nullptr;
        vk::Buffer cameraUBO;
        vk::DeviceMemory cameraUBOMemory;
        void* cameraUBOMapped = nullptr;

        uint32_t maxInstances = 1024;
        uint32_t currentInstanceCount = 0;

        vk::Image defaultTextureImage;
        vk::DeviceMemory defaultTextureMemory;
        vk::ImageView defaultTextureImageView;
        vk::Sampler textureSampler;

        std::unique_ptr<core::Texture> customTexture;
        std::string currentTexturePath;

        VFXRibbonPreviewPushConstants pushConstants;

    public:
        explicit VFXRibbonPreviewPipeline(core::Device& device, core::SwapChain& swapChain,
                                           core::OffscreenResources& offscreenResources);
        ~VFXRibbonPreviewPipeline();

        void init();
        void recreate();
        void cleanUp();

        void updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                             const glm::vec3& cameraPos, float time) const;

        void setRibbonSegments(const std::vector<VFXRibbonSegmentData>& segments);

        void setTexture(const std::string& texturePath);
        void setRenderingConfig(float alphaClipThreshold, bool additiveBlend,
                                float ribbonWidth,
                                const glm::vec3& glowColor = glm::vec3(1.0f),
                                float uvScrollSpeedU = 0.0f, float uvScrollSpeedV = 0.0f);

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

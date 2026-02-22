#pragma once

#include "GPUVFXTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>
#include <string>
#include <unordered_map>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
    class Texture;
}

namespace render::vfx
{
    class VFXSceneGPUPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        bool initialized = false;
        mutable bool descriptorsNeedUpdate = true;  // mutable: caching flag for lazy descriptor writes

        std::shared_ptr<core::Shader> gpuShader;

        vk::RenderPass externalRenderPass;
        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;

        // Buffers
        vk::Buffer quadVertexBuffer;
        vk::DeviceMemory quadVertexBufferMemory;
        vk::Buffer quadIndexBuffer;
        vk::DeviceMemory quadIndexBufferMemory;
        vk::Buffer cameraUBO;
        vk::DeviceMemory cameraUBOMemory;
        void* cameraUBOMapped = nullptr;  // Persistently mapped for efficient per-frame updates

        // Cached particle buffer info
        vk::Buffer cachedParticleBuffer;
        vk::DeviceSize cachedParticleBufferSize = 0;

        vk::Buffer cachedConfigBuffer;
        vk::DeviceSize cachedConfigBufferSize = 0;

        // Default texture (white 1x1)
        vk::Image defaultTextureImage;
        vk::DeviceMemory defaultTextureMemory;
        vk::ImageView defaultTextureImageView;
        vk::Sampler textureSampler;

        // Scene depth texture for soft particles (VK-494)
        vk::ImageView sceneDepthImageView;
        vk::Sampler depthSampler;

        // Per-emitter texture and rendering config
        static constexpr uint32_t MAX_TEXTURE_SLOTS = 16;

        struct EmitterRenderConfig
        {
            std::string texturePath;
            float alphaClipThreshold = 0.1f;
            uint32_t blendMode = 0;
            uint32_t renderMode = 0; // VK-496: skip mesh particle emitters
            glm::vec3 glowColor{1.0f, 1.0f, 1.0f};
        };

        struct TextureEntry
        {
            std::unique_ptr<core::Texture> texture;
            vk::DescriptorSet descriptorSet;
        };

        std::unordered_map<uint32_t, EmitterRenderConfig> emitterConfigs;
        std::unordered_map<std::string, TextureEntry> textureEntries;
        vk::DescriptorSet defaultDescriptorSet;

    public:
        explicit VFXSceneGPUPipeline(core::Device& device, core::SwapChain& swapChain);
        ~VFXSceneGPUPipeline();

        VFXSceneGPUPipeline(const VFXSceneGPUPipeline&) = delete;
        VFXSceneGPUPipeline& operator=(const VFXSceneGPUPipeline&) = delete;

        void init(vk::RenderPass renderPass);
        void recreate(vk::RenderPass renderPass);
        void cleanup();
        bool isInitialized() const { return initialized; }

        void updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                             const glm::vec3& cameraPos, float time,
                             float nearPlane = 0.1f, float farPlane = 1000.0f) const;

        void setSceneDepthImageView(vk::ImageView depthView);

        void updateParticleBuffer(vk::Buffer particleBuffer, vk::DeviceSize particleBufferSize);
        void updateConfigBuffer(vk::Buffer configBuffer, vk::DeviceSize configBufferSize);

        void setEmitterTexture(uint32_t emitterIndex, const std::string& texturePath);
        void setEmitterRenderingConfig(uint32_t emitterIndex, float alphaClipThreshold, bool additiveBlend,
                                       const glm::vec3& glowColor = glm::vec3(1.0f));
        void setEmitterRenderMode(uint32_t emitterIndex, uint32_t renderMode);
        void removeEmitter(uint32_t emitterIndex);

        void recordCommandsInline(
            vk::CommandBuffer cmd,
            vk::Buffer drawCommandBuffer,
            uint32_t emitterCount) const;

    private:
        void loadShader();
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void createPipeline();
        void createBuffers();
        void createDefaultTexture();
        void createSampler();
        void createDepthSampler();
        void writeDescriptors() const;
        void writeDescriptorSet(vk::DescriptorSet dstSet, core::Texture* texture) const;
        vk::DescriptorSet allocateDescriptorSetFromPool();
    };
}

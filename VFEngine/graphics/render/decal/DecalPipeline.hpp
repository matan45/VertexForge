#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include "../../../services/providers/render/IDecalRenderProvider.hpp"

namespace core
{
    class Device;
    class SwapChain;
    class Texture;
    struct OffscreenResources;
}

namespace render::decal
{
    struct DecalGPUData
    {
        glm::mat4 inverseDecalMatrix;
        glm::vec4 color;
        glm::vec4 fadeParams; // x=angleFadeStart, y=angleFadeEnd, z=edgeFalloff, w=normalStrength
        glm::vec4 halfExtents; // xyz=halfExtents, w=hasAlbedoTexture (0 or 1)
    };

    struct DecalPushConstants
    {
        glm::mat4 decalWorldMatrix;
        uint32_t decalIndex;
        float padding[3];
    };

    class DecalPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;

        bool initialized = false;

        vk::RenderPass decalRenderPass;
        std::vector<vk::Framebuffer> decalFramebuffers;

        vk::Pipeline pipeline;
        vk::PipelineLayout pipelineLayout;

        // Set 0: camera UBO, depth texture, decal data SSBO
        vk::DescriptorSetLayout globalDescriptorSetLayout;
        vk::DescriptorPool globalDescriptorPool;
        vk::DescriptorSet globalDescriptorSet;

        // Set 1: per-decal albedo texture
        vk::DescriptorSetLayout textureDescriptorSetLayout;
        vk::DescriptorPool textureDescriptorPool;

        vk::Sampler depthSampler;
        vk::Sampler textureSampler;

        vk::Buffer decalDataBuffer;
        vk::DeviceMemory decalDataMemory;
        uint32_t maxDecals = 256;

        vk::Buffer cubeVertexBuffer;
        vk::DeviceMemory cubeVertexMemory;
        vk::Buffer cubeIndexBuffer;
        vk::DeviceMemory cubeIndexMemory;
        uint32_t cubeIndexCount = 0;

        struct CameraUBO
        {
            glm::mat4 viewProjection;
            glm::mat4 inverseViewProjection;
            glm::vec4 cameraParams; // x=nearPlane, y=farPlane, z=screenWidth, w=screenHeight
        };

        vk::Buffer cameraUBOBuffer;
        vk::DeviceMemory cameraUBOMemory;

        std::vector<DecalGPUData> gpuDecalData;
        std::vector<services::DecalRenderData> currentDecals;

        // Texture cache: path -> loaded texture + descriptor set
        struct TextureEntry
        {
            std::unique_ptr<core::Texture> texture;
            vk::DescriptorSet descriptorSet;
        };
        std::unordered_map<std::string, TextureEntry> textureCache;
        static constexpr uint32_t MAX_CACHED_TEXTURES = 64;

        // Fallback 1x1 white texture for decals without albedo
        std::unique_ptr<core::Texture> fallbackTexture;
        vk::DescriptorSet fallbackDescriptorSet;

        glm::mat4 currentViewProjection{1.0f};
        glm::mat4 currentInverseViewProjection{1.0f};
        float currentNearPlane = 0.1f;
        float currentFarPlane = 1000.0f;

    public:
        explicit DecalPipeline(core::Device& device, core::SwapChain& swapChain,
                               core::OffscreenResources& offscreenResources);
        ~DecalPipeline();

        void init();
        void cleanup();
        void recreate();

        void updateDecals(const std::vector<services::DecalRenderData>& decals);
        void setCameraData(const glm::mat4& view, const glm::mat4& projection,
                           float nearPlane, float farPlane);

        void render(const vk::CommandBuffer& cmd, uint32_t imageIndex);

        bool hasDecals() const { return !currentDecals.empty(); }
        bool isInitialized() const { return initialized; }

    private:
        void createRenderPass();
        void createFramebuffers();
        void createSamplers();
        void createDescriptorResources();
        void createPipeline();
        void createCubeGeometry();
        void createBuffers();
        void createFallbackTexture();

        void updateGlobalDescriptorSet();
        void uploadDecalData();
        void uploadCameraUBO();

        vk::DescriptorSet getOrLoadTexture(const std::string& path);

        void transitionDepthToReadOnly(const vk::CommandBuffer& cmd);
        void transitionDepthToAttachment(const vk::CommandBuffer& cmd);
    };
}

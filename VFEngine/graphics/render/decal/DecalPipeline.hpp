#pragma once

#include <vulkan/vulkan.hpp>
#include "../../core/VulkanMemoryManager.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include "../../../services/providers/render/IDecalRenderProvider.hpp"
#include "../common/CameraTypes.hpp"

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
        glm::vec4 textureFlags; // x=hasAlbedo, y=hasNormal, z=hasORM, w=unused
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

        vk::Pipeline pipeline;
        vk::PipelineLayout pipelineLayout;

        // Set 0: camera UBO, depth texture, decal data SSBO
        vk::DescriptorSetLayout globalDescriptorSetLayout;
        vk::DescriptorPool globalDescriptorPool;
        vk::DescriptorSet globalDescriptorSet;

        // Set 1: per-decal textures (albedo, normal, ORM)
        vk::DescriptorSetLayout textureDescriptorSetLayout;
        vk::DescriptorPool textureDescriptorPool;

        vk::Sampler depthSampler;
        vk::Sampler textureSampler;

        vk::Buffer decalDataBuffer;
        core::VulkanAllocation decalDataAllocation;
        uint32_t maxDecals = 256;

        vk::Buffer cubeVertexBuffer;
        core::VulkanAllocation cubeVertexAllocation;
        vk::Buffer cubeIndexBuffer;
        core::VulkanAllocation cubeIndexAllocation;
        uint32_t cubeIndexCount = 0;

        using CameraUBO = render::common::GPUCameraData;

        vk::Buffer cameraUBOBuffer;
        core::VulkanAllocation cameraUBOAllocation;

        std::vector<DecalGPUData> gpuDecalData;
        std::vector<services::DecalRenderData> currentDecals;

        // Individual texture cache: path -> loaded Texture
        std::unordered_map<std::string, std::unique_ptr<core::Texture>> textureCache;

        // Per-decal descriptor set cache: "albedo|normal|orm" key -> descriptor set
        std::unordered_map<std::string, vk::DescriptorSet> decalDescriptorCache;
        static constexpr uint32_t MAX_CACHED_TEXTURES = 128;

        // Fallback 1x1 textures
        std::unique_ptr<core::Texture> fallbackWhiteTexture;
        std::unique_ptr<core::Texture> fallbackNormalTexture; // flat normal (0.5, 0.5, 1.0)
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
        void createSamplers();
        void createDescriptorResources();
        void createPipeline();
        void createCubeGeometry();
        void createBuffers();
        void createFallbackTexture();

        void updateGlobalDescriptorSet();
        void uploadDecalData();
        void uploadCameraUBO();

        vk::DescriptorSet getOrCreateDecalTextureSet(const std::string& albedoPath,
                                                       const std::string& normalPath,
                                                       const std::string& ormPath);
        core::Texture* getOrLoadTexture(const std::string& path, vk::Format format);

        void transitionDepthToReadOnly(const vk::CommandBuffer& cmd);
        void transitionDepthToAttachment(const vk::CommandBuffer& cmd);
    };
}

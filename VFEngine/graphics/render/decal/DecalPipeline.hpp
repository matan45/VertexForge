#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <vector>
#include "../../../services/providers/render/IDecalRenderProvider.hpp"

namespace core
{
    class Device;
    class SwapChain;
    struct OffscreenResources;
}

namespace render::decal
{
    struct DecalGPUData
    {
        glm::mat4 inverseDecalMatrix;
        glm::vec4 color;
        glm::vec4 fadeParams; // x=angleFadeStart, y=angleFadeEnd, z=edgeFalloff, w=normalStrength
        glm::vec4 halfExtents; // xyz=halfExtents, w=modifyNormals (0 or 1)
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

        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        vk::Sampler depthSampler;

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
        void createSampler();
        void createDescriptorResources();
        void createPipeline();
        void createCubeGeometry();
        void createBuffers();

        void updateDescriptorSet();
        void uploadDecalData();
        void uploadCameraUBO();

        void transitionDepthToReadOnly(const vk::CommandBuffer& cmd);
        void transitionDepthToAttachment(const vk::CommandBuffer& cmd);
    };
}

#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
}

namespace render::mesh
{
    // Data needed to render a camera frustum
    struct CameraFrustumRenderData
    {
        glm::mat4 projectionMatrix;
        glm::mat4 worldMatrix;  // Camera entity's world transform
        bool showFrustum = false;
    };

    // Push constants for frustum wireframe rendering
    // Total size: 64 + 64 + 16 = 144 bytes (within 256 byte limit of most GPUs)
    struct FrustumPushConstants
    {
        glm::mat4 viewProj;          // Editor's view-projection matrix
        glm::mat4 inverseViewProj;   // Camera's inverse view-projection
        glm::vec4 color;
    };

    class FrustumDebugRenderer
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        std::shared_ptr<core::Shader> wireframeShader;

        vk::Pipeline wireframePipeline;
        vk::PipelineLayout wireframePipelineLayout;

        // Static vertex buffer with NDC corners (never changes)
        vk::Buffer vertexBuffer;
        vk::DeviceMemory vertexBufferMemory;
        vk::Buffer indexBuffer;
        vk::DeviceMemory indexBufferMemory;

        bool initialized = false;

    public:
        FrustumDebugRenderer(core::Device& device, core::SwapChain& swapChain);
        ~FrustumDebugRenderer();

        void init(vk::RenderPass renderPass);
        void recreate(vk::RenderPass renderPass);
        void cleanUp();
        void cleanUpShader();

        void render(const vk::CommandBuffer& commandBuffer,
                    const std::vector<CameraFrustumRenderData>& cameraDrawList,
                    const glm::mat4& editorView,
                    const glm::mat4& editorProjection) const;

        bool isInitialized() const { return initialized; }

    private:
        void loadShader();
        void createPipeline(vk::RenderPass renderPass);
        void createBuffers();
    };
}

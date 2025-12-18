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
    struct FrustumPushConstants
    {
        glm::mat4 mvp;
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

        // Dynamic vertex buffer for frustum corners (updated each frame)
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

        // Compute frustum corners from inverse view-projection matrix
        std::vector<glm::vec3> computeFrustumCorners(const glm::mat4& inverseViewProj) const;
    };
}

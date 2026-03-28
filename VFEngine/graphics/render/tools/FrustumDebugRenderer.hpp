#pragma once

#include "DebugRendererBase.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <array>

namespace core
{
    class Shader;
}

namespace render::mesh
{
    
    struct CameraFrustumRenderData
    {
        glm::mat4 projectionMatrix;
        glm::mat4 worldMatrix;  // Camera entity's world transform
        bool showFrustum = false;
    };
    
    struct FrustumPushConstants
    {
        glm::mat4 viewProj;          // Editor's view-projection matrix
        glm::mat4 inverseViewProj;   // Camera's inverse view-projection
        glm::vec4 color;
    };

    class FrustumDebugRenderer : public DebugRendererBase
    {
    private:
        // Static NDC corners for frustum (Vulkan: z = 0 near, z = 1 far)
        inline static constexpr std::array<glm::vec3, 8> ndcCorners = {{
            // Near plane (z = 0 in Vulkan)
            {-1.0f, -1.0f, 0.0f},  // bottom-left
            { 1.0f, -1.0f, 0.0f},  // bottom-right
            { 1.0f,  1.0f, 0.0f},  // top-right
            {-1.0f,  1.0f, 0.0f},  // top-left
            // Far plane (z = 1 in Vulkan)
            {-1.0f, -1.0f, 1.0f},  // bottom-left
            { 1.0f, -1.0f, 1.0f},  // bottom-right
            { 1.0f,  1.0f, 1.0f},  // top-right
            {-1.0f,  1.0f, 1.0f},  // top-left
        }};

        // Line indices for 12 edges of the frustum
        // Near plane: 0-1-2-3, Far plane: 4-5-6-7
        inline static constexpr std::array<uint32_t, 24> indices = {{
            // Near plane edges
            0, 1,  1, 2,  2, 3,  3, 0,
            // Far plane edges
            4, 5,  5, 6,  6, 7,  7, 4,
            // Connecting edges (near to far)
            0, 4,  1, 5,  2, 6,  3, 7
        }};

        std::shared_ptr<core::Shader> wireframeShader;

        vk::Pipeline wireframePipeline;
        vk::PipelineLayout wireframePipelineLayout;

        // Static vertex buffer with NDC corners (never changes)
        vk::Buffer vertexBuffer;
        core::VulkanAllocation vertexBufferAllocation;
        vk::Buffer indexBuffer;
        core::VulkanAllocation indexBufferAllocation;

    public:
        explicit FrustumDebugRenderer(core::Device& device, core::SwapChain& swapChain);
        ~FrustumDebugRenderer();

        void init(vk::RenderPass renderPass);
        void recreate(vk::RenderPass renderPass);
        void cleanUp();
        void cleanUpShader();

        void render(const vk::CommandBuffer& commandBuffer,
                    const std::vector<CameraFrustumRenderData>& cameraDrawList,
                    const glm::mat4& editorView,
                    const glm::mat4& editorProjection) const;

    private:
        void loadShader();
        void createPipeline(vk::RenderPass renderPass);
        void createBuffers();
    };
}

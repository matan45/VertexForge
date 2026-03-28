#pragma once

#include "DebugRendererBase.hpp"
#include "../mesh/MeshTypes.hpp"
#include <memory>
#include <vector>
#include <array>

namespace core
{
    class Shader;
}

namespace render::mesh
{
    inline constexpr std::array<glm::vec3, 8> kUnitCubeVertices = {{
        {-1.0f, -1.0f, -1.0f},  // 0: back-bottom-left
        { 1.0f, -1.0f, -1.0f},  // 1: back-bottom-right
        { 1.0f,  1.0f, -1.0f},  // 2: back-top-right
        {-1.0f,  1.0f, -1.0f},  // 3: back-top-left
        {-1.0f, -1.0f,  1.0f},  // 4: front-bottom-left
        { 1.0f, -1.0f,  1.0f},  // 5: front-bottom-right
        { 1.0f,  1.0f,  1.0f},  // 6: front-top-right
        {-1.0f,  1.0f,  1.0f},  // 7: front-top-left
    }};

    inline constexpr std::array<uint32_t, 24> kUnitCubeLineIndices = {{
        0, 1,  1, 2,  2, 3,  3, 0,  // Back face edges
        4, 5,  5, 6,  6, 7,  7, 4,  // Front face edges
        0, 4,  1, 5,  2, 6,  3, 7   // Connecting edges
    }};
    class AABBDebugRenderer : public DebugRendererBase
    {
    private:
        std::shared_ptr<core::Shader> wireframeShader;

        vk::Pipeline wireframePipeline;
        vk::PipelineLayout wireframePipelineLayout;

        vk::Buffer vertexBuffer;
        core::VulkanAllocation vertexBufferAllocation;
        vk::Buffer indexBuffer;
        core::VulkanAllocation indexBufferAllocation;

    public:
        explicit AABBDebugRenderer(core::Device& device, core::SwapChain& swapChain);
        ~AABBDebugRenderer();

        void init(vk::RenderPass renderPass);
        void recreate(vk::RenderPass renderPass);
        void cleanUp();
        void cleanUpShader();

        void render(const vk::CommandBuffer& commandBuffer,
                    const std::vector<MeshRenderData>& meshDrawList,
                    const glm::mat4& view,
                    const glm::mat4& projection,
                    const std::function<const MeshGPUData*(const std::string&)>& getMeshFunc) const;

    private:
        void loadShader();
        void createPipeline(vk::RenderPass renderPass);
        void createBuffers();

        void renderAABB(const vk::CommandBuffer& commandBuffer,
                        const math::AABB& aabb,
                        const glm::mat4& modelMatrix,
                        const glm::mat4& viewProjection,
                        const glm::vec4& color) const;
    };
}

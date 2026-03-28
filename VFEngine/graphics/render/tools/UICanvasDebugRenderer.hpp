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

    struct UICanvasOutlineRenderData
    {
        glm::mat4 modelMatrix; // worldMatrix * scale(refWidth/pixelsPerUnit, refHeight/pixelsPerUnit, 1)
    };

    class UICanvasDebugRenderer : public DebugRendererBase
    {
    private:
        // Unit quad corners (centered at origin)
        // Vertices 0-3: quad corners
        // Vertices 4-11: corner anchor offset vertices (L-shaped marks)
        static constexpr float cornerSize = 0.1f;

        inline static constexpr std::array<glm::vec3, 12> vertices = {{
            // Quad corners
            {-0.5f, -0.5f, 0.0f}, // 0: bottom-left
            { 0.5f, -0.5f, 0.0f}, // 1: bottom-right
            { 0.5f,  0.5f, 0.0f}, // 2: top-right
            {-0.5f,  0.5f, 0.0f}, // 3: top-left

            // Corner anchor offsets (L-shaped marks inward from each corner)
            // Corner 0 (bottom-left)
            {-0.5f + cornerSize, -0.5f, 0.0f},  // 4: horizontal inward
            {-0.5f, -0.5f + cornerSize, 0.0f},   // 5: vertical inward
            // Corner 1 (bottom-right)
            { 0.5f - cornerSize, -0.5f, 0.0f},   // 6: horizontal inward
            { 0.5f, -0.5f + cornerSize, 0.0f},    // 7: vertical inward
            // Corner 2 (top-right)
            { 0.5f - cornerSize,  0.5f, 0.0f},    // 8: horizontal inward
            { 0.5f,  0.5f - cornerSize, 0.0f},     // 9: vertical inward
            // Corner 3 (top-left)
            {-0.5f + cornerSize,  0.5f, 0.0f},    // 10: horizontal inward
            {-0.5f,  0.5f - cornerSize, 0.0f},     // 11: vertical inward
        }};

        // Line indices: first 8 = outline edges, next 16 = corner L-marks
        inline static constexpr std::array<uint32_t, 24> indices = {{
            // Outline edges (4 edges, 8 indices)
            0, 1,  1, 2,  2, 3,  3, 0,
            // Corner anchor L-marks (8 lines, 16 indices)
            0, 4,  0, 5,   // corner 0
            1, 6,  1, 7,   // corner 1
            2, 8,  2, 9,   // corner 2
            3, 10, 3, 11   // corner 3
        }};

        std::shared_ptr<core::Shader> wireframeShader;

        vk::Pipeline wireframePipeline;
        vk::PipelineLayout wireframePipelineLayout;

        vk::Buffer vertexBuffer;
        core::VulkanAllocation vertexBufferAllocation;
        vk::Buffer indexBuffer;
        core::VulkanAllocation indexBufferAllocation;

    public:
        explicit UICanvasDebugRenderer(core::Device& device, core::SwapChain& swapChain);
        ~UICanvasDebugRenderer();

        void init(vk::RenderPass renderPass);
        void recreate(vk::RenderPass renderPass);
        void cleanUp();
        void cleanUpShader();

        void render(const vk::CommandBuffer& commandBuffer,
                    const std::vector<UICanvasOutlineRenderData>& canvasDrawList,
                    const glm::mat4& editorView,
                    const glm::mat4& editorProjection) const;

    private:
        void loadShader();
        void createPipeline(vk::RenderPass renderPass);
        void createBuffers();
    };
}

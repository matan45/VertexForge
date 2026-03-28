#pragma once

#include "DebugRendererBase.hpp"
#include <glm/glm.hpp>
#include <memory>

namespace core
{
    class Shader;
}

namespace render::mesh
{
    
    struct GridPushConstants
    {
        glm::mat4 viewProj;     // 64 bytes - View-Projection matrix
        glm::vec4 gridColor;    // 16 bytes - Grid line color (RGBA)
        glm::vec4 axisColorX;   // 16 bytes - X-axis center line color (red)
        glm::vec4 axisColorZ;   // 16 bytes - Z-axis center line color (blue)
        glm::vec4 gridParams;   // 16 bytes - x: gridSize, y: cellSize, z: fadeStart, w: fadeEnd
    };

    class GridRenderer : public DebugRendererBase
    {
    private:
        std::shared_ptr<core::Shader> gridShader;

        vk::Pipeline gridPipeline;
        vk::PipelineLayout gridPipelineLayout;

        vk::Buffer vertexBuffer;
        core::VulkanAllocation vertexBufferAllocation;
        vk::Buffer indexBuffer;
        core::VulkanAllocation indexBufferAllocation;

        uint32_t indexCount = 0;

        bool visible = true;

        // Grid parameters
        float gridSize = 100.0f;      // Total grid extent from -gridSize to +gridSize
        float cellSize = 1.0f;        // Size of each grid cell
        float fadeStart = 80.0f;      // Distance at which fade begins
        float fadeEnd = 100.0f;       // Distance at which fully faded
        glm::vec4 gridColor = glm::vec4(0.5f, 0.5f, 0.5f, 0.5f);  // Gray with transparency
        glm::vec4 axisColorX = glm::vec4(0.8f, 0.2f, 0.2f, 0.8f); // Red for X-axis
        glm::vec4 axisColorZ = glm::vec4(0.2f, 0.2f, 0.8f, 0.8f); // Blue for Z-axis

    public:
        explicit GridRenderer(core::Device& device, core::SwapChain& swapChain);
        ~GridRenderer();

        void init(vk::RenderPass renderPass);
        void recreate(vk::RenderPass renderPass);
        void cleanUp();
        void cleanUpShader();

        void render(const vk::CommandBuffer& commandBuffer,
                    const glm::mat4& view,
                    const glm::mat4& projection) const;

        void setVisible(bool show) { visible = show; }
        bool isVisible() const { return visible; }

    private:
        void loadShader();
        void createPipeline(vk::RenderPass renderPass);
        void createBuffers();
    };
}

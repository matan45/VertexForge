#pragma once

#include "DebugRendererBase.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace core
{
    class Shader;
}

namespace render::mesh
{
    struct NavmeshDebugPushConstants
    {
        glm::mat4 mvp;
        glm::vec4 color;
    };

    class NavmeshDebugRenderer : public DebugRendererBase
    {
    private:
        std::shared_ptr<core::Shader> wireframeShader;

        vk::Pipeline wireframePipeline;
        vk::PipelineLayout wireframePipelineLayout;

        vk::Buffer vertexBuffer;
        vk::DeviceMemory vertexMemory;
        vk::Buffer indexBuffer;
        vk::DeviceMemory indexMemory;
        uint32_t indexCount = 0;

        bool hasData = false;

    public:
        explicit NavmeshDebugRenderer(core::Device& device, core::SwapChain& swapChain);
        ~NavmeshDebugRenderer();

        void init(vk::RenderPass renderPass);
        void recreate(vk::RenderPass renderPass);
        void cleanUp();
        void cleanUpShader();

        void updateMesh(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& triangleIndices);
        void clearMesh();

        void render(const vk::CommandBuffer& commandBuffer,
                    const glm::mat4& view,
                    const glm::mat4& projection) const;

        bool hasMeshData() const { return hasData; }

    private:
        void loadShader();
        void createPipeline(vk::RenderPass renderPass);
        void destroyMeshBuffers();
    };
}

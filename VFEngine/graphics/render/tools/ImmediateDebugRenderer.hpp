#pragma once

#include "DebugRendererBase.hpp"
#include "ImmediateDebugTypes.hpp"
#include <glm/glm.hpp>
#include <memory>

namespace core
{
    class Shader;
}

namespace render::mesh
{
    struct ImmediateDebugPushConstants
    {
        glm::mat4 viewProj;
    };

    class ImmediateDebugRenderer : public DebugRendererBase
    {
    private:
        std::shared_ptr<core::Shader> shader;

        vk::Pipeline pipeline;
        vk::PipelineLayout pipelineLayout;

        vk::Buffer vertexBuffer;
        vk::DeviceMemory vertexMemory;
        uint32_t currentVertexCount = 0;
        vk::DeviceSize currentBufferSize = 0;

    public:
        explicit ImmediateDebugRenderer(core::Device& device, core::SwapChain& swapChain);
        ~ImmediateDebugRenderer();

        void init(vk::RenderPass renderPass);
        void recreate(vk::RenderPass renderPass);
        void cleanUp();
        void cleanUpShader();

        // Upload new draw data for this frame. Rebuilds the GPU buffer.
        void updateDrawList(ImmediateDebugDrawList drawList);

        void render(const vk::CommandBuffer& commandBuffer,
                    const glm::mat4& view,
                    const glm::mat4& projection) const;

    private:
        void loadShader();
        void createPipeline(vk::RenderPass renderPass);
        void destroyVertexBuffer();
    };
}

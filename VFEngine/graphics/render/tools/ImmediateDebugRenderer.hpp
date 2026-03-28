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
        core::VulkanAllocation vertexAllocation;
        uint32_t currentVertexCount = 0;
        vk::DeviceSize currentBufferSize = 0;

    public:
        explicit ImmediateDebugRenderer(core::Device& device, core::SwapChain& swapChain);
        ~ImmediateDebugRenderer();

        void init(vk::RenderPass renderPass);
        void recreate(vk::RenderPass renderPass);
        void cleanUp();
        void cleanUpShader();

        void updateDrawList(const ImmediateDebugDrawList& drawList);

        void render(const vk::CommandBuffer& commandBuffer,
                    const glm::mat4& view,
                    const glm::mat4& projection) const;

        [[nodiscard]] bool hasData() const { return currentVertexCount > 0; }

    private:
        void loadShader();
        void createPipeline(vk::RenderPass renderPass);
        void destroyVertexBuffer();
    };
}

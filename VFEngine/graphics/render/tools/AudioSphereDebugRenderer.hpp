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
    struct AudioSphereRenderData
    {
        glm::vec3 position;
        float minDistance;
        float maxDistance;
        bool showDebugSpheres = false;

        float innerConeAngle = 360.0f;
        float outerConeAngle = 360.0f;
        glm::vec3 direction{0.0f, 0.0f, -1.0f};
        bool showDebugCone = false;
    };

    struct AudioSpherePushConstants
    {
        glm::mat4 mvp;
        glm::vec4 color;
    };

    class AudioSphereDebugRenderer : public DebugRendererBase
    {
    private:
        std::shared_ptr<core::Shader> wireframeShader;

        vk::Pipeline wireframePipeline;
        vk::PipelineLayout wireframePipelineLayout;

        vk::Buffer vertexBuffer;
        core::VulkanAllocation vertexBufferAllocation;
        vk::Buffer indexBuffer;
        core::VulkanAllocation indexBufferAllocation;

        uint32_t indexCount = 0;

        vk::Buffer coneVertexBuffer;
        core::VulkanAllocation coneVertexBufferAllocation;
        vk::Buffer coneIndexBuffer;
        core::VulkanAllocation coneIndexBufferAllocation;
        uint32_t coneIndexCount = 0;

        static constexpr int SPHERE_SEGMENTS = 32;
        static constexpr int CONE_SEGMENTS = 24;

    public:
        explicit AudioSphereDebugRenderer(core::Device& device, core::SwapChain& swapChain);
        ~AudioSphereDebugRenderer();

        void init(vk::Format colorFormat, vk::Format depthFormat);
        void recreate(vk::Format colorFormat, vk::Format depthFormat);
        void cleanUp();
        void cleanUpShader();

        void render(const vk::CommandBuffer& commandBuffer,
                    const std::vector<AudioSphereRenderData>& audioSourceDrawList,
                    const glm::mat4& view,
                    const glm::mat4& projection) const;

    private:
        void loadShader();
        void createPipeline(vk::Format colorFormat, vk::Format depthFormat);
        void createBuffers();
        void createConeBuffers();

        void renderCones(const vk::CommandBuffer& commandBuffer,
                         const std::vector<AudioSphereRenderData>& audioSourceDrawList,
                         const glm::mat4& viewProj) const;
    };
}

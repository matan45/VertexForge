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
        vk::DeviceMemory vertexBufferMemory;
        vk::Buffer indexBuffer;
        vk::DeviceMemory indexBufferMemory;

        uint32_t indexCount = 0;

        static constexpr int SPHERE_SEGMENTS = 32;

    public:
        explicit AudioSphereDebugRenderer(core::Device& device, core::SwapChain& swapChain);
        ~AudioSphereDebugRenderer();

        void init(vk::RenderPass renderPass);
        void recreate(vk::RenderPass renderPass);
        void cleanUp();
        void cleanUpShader();

        void render(const vk::CommandBuffer& commandBuffer,
                    const std::vector<AudioSphereRenderData>& audioSourceDrawList,
                    const glm::mat4& view,
                    const glm::mat4& projection) const;

    private:
        void loadShader();
        void createPipeline(vk::RenderPass renderPass);
        void createBuffers();
    };
}

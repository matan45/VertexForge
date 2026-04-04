#pragma once

#include "DebugRendererBase.hpp"
#include "../shadow/ShadowTypes.hpp"
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
    enum class ShadowFrustumType : uint8_t
    {
        DirectionalCascade,
        SpotFrustum,
        PointSphere
    };

    struct ShadowFrustumRenderData
    {
        ShadowFrustumType type = ShadowFrustumType::DirectionalCascade;
        uint32_t cascadeIndex = 0;
        glm::mat4 inverseViewProjection{1.0f};
        glm::vec3 lightPosition{0.0f};
        float radius = 0.0f;
    };

    struct ShadowDebugPushConstants
    {
        glm::mat4 viewProj;
        glm::mat4 inverseViewProj;
        glm::vec4 color;
    };

    // Color scheme: Cascade 0=Red, 1=Orange, 2=Yellow, 3=Green, Spot=Cyan, Point=Magenta
    class ShadowDebugRenderer : public DebugRendererBase
    {
    private:
        inline static constexpr std::array<glm::vec3, 8> ndcCorners = {{
            {-1.0f, -1.0f, 0.0f}, { 1.0f, -1.0f, 0.0f},
            { 1.0f,  1.0f, 0.0f}, {-1.0f,  1.0f, 0.0f},
            {-1.0f, -1.0f, 1.0f}, { 1.0f, -1.0f, 1.0f},
            { 1.0f,  1.0f, 1.0f}, {-1.0f,  1.0f, 1.0f},
        }};

        inline static constexpr std::array<uint32_t, 24> frustumIndices = {{
            0, 1,  1, 2,  2, 3,  3, 0,
            4, 5,  5, 6,  6, 7,  7, 4,
            0, 4,  1, 5,  2, 6,  3, 7
        }};

        inline static constexpr std::array<glm::vec4, 4> cascadeColors = {{
            {1.0f, 0.2f, 0.2f, 0.8f},
            {1.0f, 0.6f, 0.2f, 0.8f},
            {1.0f, 1.0f, 0.2f, 0.8f},
            {0.2f, 1.0f, 0.2f, 0.8f},
        }};

        inline static constexpr glm::vec4 spotLightColor = {0.2f, 0.8f, 1.0f, 0.8f};
        inline static constexpr glm::vec4 pointLightColor = {1.0f, 0.2f, 1.0f, 0.8f};

        static constexpr int SPHERE_SEGMENTS = 32;

        std::shared_ptr<core::Shader> frustumShader;
        std::shared_ptr<core::Shader> sphereShader;

        vk::Pipeline frustumPipeline;
        vk::PipelineLayout frustumPipelineLayout;
        vk::Pipeline spherePipeline;
        vk::PipelineLayout spherePipelineLayout;

        vk::Buffer frustumVertexBuffer;
        core::VulkanAllocation frustumVertexBufferAllocation;
        vk::Buffer frustumIndexBuffer;
        core::VulkanAllocation frustumIndexBufferAllocation;

        vk::Buffer sphereVertexBuffer;
        core::VulkanAllocation sphereVertexBufferAllocation;
        vk::Buffer sphereIndexBuffer;
        core::VulkanAllocation sphereIndexBufferAllocation;
        uint32_t sphereIndexCount = 0;

    public:
        explicit ShadowDebugRenderer(core::Device& device, core::SwapChain& swapChain);
        ~ShadowDebugRenderer();

        void init(vk::Format colorFormat, vk::Format depthFormat);
        void recreate(vk::Format colorFormat, vk::Format depthFormat);
        void cleanUp();
        void cleanUpShader();

        void render(const vk::CommandBuffer& commandBuffer,
                    const std::vector<ShadowFrustumRenderData>& shadowDrawList,
                    const glm::mat4& editorView,
                    const glm::mat4& editorProjection) const;

    private:
        void loadShaders();
        void createPipelines(vk::Format colorFormat, vk::Format depthFormat);
        void createFrustumBuffers();
        void createSphereBuffers();

        static glm::vec4 getColorForShadow(ShadowFrustumType type, uint32_t cascadeIndex);

        void renderFrustum(const vk::CommandBuffer& commandBuffer,
                           const ShadowFrustumRenderData& shadow,
                           const glm::mat4& editorViewProj) const;

        void renderSphere(const vk::CommandBuffer& commandBuffer,
                          const ShadowFrustumRenderData& shadow,
                          const glm::mat4& editorViewProj) const;
    };
}

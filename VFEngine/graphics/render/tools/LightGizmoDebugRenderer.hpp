#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
}

namespace render::mesh
{
    enum class LightGizmoType : uint8_t
    {
        Directional,
        Point,
        Spot
    };

    struct LightGizmoRenderData
    {
        LightGizmoType type;
        glm::mat4 worldMatrix;
        glm::vec3 color;
        float radius = 10.0f;
        float innerAngle = 30.0f;   // degrees
        float outerAngle = 45.0f;   // degrees
        float range = 20.0f;
    };

    struct LightGizmoPushConstants
    {
        glm::mat4 mvp;
        glm::vec4 color;
    };

    class LightGizmoDebugRenderer
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        std::shared_ptr<core::Shader> wireframeShader;

        vk::Pipeline wireframePipeline;
        vk::PipelineLayout wireframePipelineLayout;

        vk::Buffer sphereVertexBuffer;
        vk::DeviceMemory sphereVertexBufferMemory;
        vk::Buffer sphereIndexBuffer;
        vk::DeviceMemory sphereIndexBufferMemory;
        uint32_t sphereIndexCount = 0;

        vk::Buffer coneVertexBuffer;
        vk::DeviceMemory coneVertexBufferMemory;
        vk::Buffer coneIndexBuffer;
        vk::DeviceMemory coneIndexBufferMemory;
        uint32_t coneIndexCount = 0;

        vk::Buffer arrowVertexBuffer;
        vk::DeviceMemory arrowVertexBufferMemory;
        vk::Buffer arrowIndexBuffer;
        vk::DeviceMemory arrowIndexBufferMemory;
        uint32_t arrowIndexCount = 0;

        bool initialized = false;

        static constexpr int SPHERE_SEGMENTS = 32;
        static constexpr int CONE_SEGMENTS = 16;
        static constexpr float ARROW_LENGTH = 5.0f;

    public:
        explicit LightGizmoDebugRenderer(core::Device& device, core::SwapChain& swapChain);
        ~LightGizmoDebugRenderer();

        void init(vk::RenderPass renderPass);
        void recreate(vk::RenderPass renderPass);
        void cleanUp();
        void cleanUpShader();

        void render(const vk::CommandBuffer& commandBuffer,
                    const std::vector<LightGizmoRenderData>& lightGizmoDrawList,
                    const glm::mat4& view,
                    const glm::mat4& projection) const;

        bool isInitialized() const { return initialized; }

    private:
        void loadShader();
        void createPipeline(vk::RenderPass renderPass);
        void createSphereBuffers();
        void createConeBuffers();
        void createArrowBuffers();

        void renderPointLight(const vk::CommandBuffer& commandBuffer,
                              const LightGizmoRenderData& light,
                              const glm::mat4& viewProj) const;

        void renderSpotLight(const vk::CommandBuffer& commandBuffer,
                             const LightGizmoRenderData& light,
                             const glm::mat4& viewProj) const;

        void renderDirectionalLight(const vk::CommandBuffer& commandBuffer,
                                    const LightGizmoRenderData& light,
                                    const glm::mat4& viewProj) const;
    };
}

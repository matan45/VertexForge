#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>
#include <cstdint>
#include "../../../utilities/types/PhysicsTypes.hpp"

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
}

namespace render::mesh
{
    struct PhysicsColliderRenderData
    {
        glm::mat4 worldMatrix;
        types::ColliderShape shape = types::ColliderShape::Box;
        glm::vec3 size{1.0f};           // Box half-extents
        float radius = 0.5f;            // Sphere/capsule radius
        float height = 2.0f;            // Capsule total height
        uint8_t bodyType = 1;           // 0=Static, 1=Dynamic, 2=Kinematic
        bool isTrigger = false;
    };

    struct PhysicsDebugPushConstants
    {
        glm::mat4 mvp;
        glm::vec4 color;
    };

    class PhysicsDebugRenderer
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        std::shared_ptr<core::Shader> wireframeShader;

        vk::Pipeline wireframePipeline;
        vk::PipelineLayout wireframePipelineLayout;

        // Box geometry buffers
        vk::Buffer boxVertexBuffer;
        vk::DeviceMemory boxVertexMemory;
        vk::Buffer boxIndexBuffer;
        vk::DeviceMemory boxIndexMemory;
        uint32_t boxIndexCount = 0;

        // Sphere geometry buffers
        vk::Buffer sphereVertexBuffer;
        vk::DeviceMemory sphereVertexMemory;
        vk::Buffer sphereIndexBuffer;
        vk::DeviceMemory sphereIndexMemory;
        uint32_t sphereIndexCount = 0;

        // Capsule geometry buffers
        vk::Buffer capsuleVertexBuffer;
        vk::DeviceMemory capsuleVertexMemory;
        vk::Buffer capsuleIndexBuffer;
        vk::DeviceMemory capsuleIndexMemory;
        uint32_t capsuleIndexCount = 0;

        bool initialized = false;

        static constexpr int SPHERE_SEGMENTS = 24;

    public:
        explicit PhysicsDebugRenderer(core::Device& device, core::SwapChain& swapChain);
        ~PhysicsDebugRenderer();

        void init(vk::RenderPass renderPass);
        void recreate(vk::RenderPass renderPass);
        void cleanUp();
        void cleanUpShader();

        void render(const vk::CommandBuffer& commandBuffer,
                    const std::vector<PhysicsColliderRenderData>& colliders,
                    const glm::mat4& view,
                    const glm::mat4& projection) const;

        bool isInitialized() const { return initialized; }

    private:
        void loadShader();
        void createPipeline(vk::RenderPass renderPass);
        void createBuffers();

        void createBoxBuffers();
        void createSphereBuffers();
        void createCapsuleBuffers();

        glm::vec4 getColorForCollider(const PhysicsColliderRenderData& data) const;
    };
}

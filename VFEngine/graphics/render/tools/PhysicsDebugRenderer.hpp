#pragma once

#include "DebugRendererBase.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include "types/PhysicsTypes.hpp"

namespace core
{
    class Shader;
}

namespace render::mesh
{
    struct PhysicsColliderRenderData
    {
        glm::mat4 worldMatrix;
        types::ColliderShape shape = types::ColliderShape::Box;
        glm::vec3 size{1.0f}; // Box full size (renderer converts to half-extents)
        float radius = 0.5f;
        float height = 2.0f;
        uint8_t bodyType = 1; // 0=Static, 1=Dynamic, 2=Kinematic
        bool isTrigger = false;
        std::string meshPath; // Path to mesh for ConvexMesh/TriangleMesh shapes

        // HeightField wireframe (world-space vertices, line-list indices)
        const std::vector<glm::vec3>* heightfieldVertices = nullptr;
        const std::vector<uint32_t>* heightfieldLineIndices = nullptr;
        std::string heightfieldCacheKey;
        uint32_t heightfieldVersion = 0;
    };

    struct MeshDebugData
    {
        vk::Buffer vertexBuffer;
        vk::DeviceMemory vertexMemory;
        vk::Buffer indexBuffer;
        vk::DeviceMemory indexMemory;
        uint32_t indexCount = 0;
        bool isValid = false;
    };

    struct PhysicsDebugPushConstants
    {
        glm::mat4 mvp;
        glm::vec4 color;
    };

    class PhysicsDebugRenderer : public DebugRendererBase
    {
    private:
        std::shared_ptr<core::Shader> wireframeShader;

        vk::Pipeline wireframePipeline;
        vk::PipelineLayout wireframePipelineLayout;

        vk::Buffer boxVertexBuffer;
        vk::DeviceMemory boxVertexMemory;
        vk::Buffer boxIndexBuffer;
        vk::DeviceMemory boxIndexMemory;
        uint32_t boxIndexCount = 0;

        vk::Buffer sphereVertexBuffer;
        vk::DeviceMemory sphereVertexMemory;
        vk::Buffer sphereIndexBuffer;
        vk::DeviceMemory sphereIndexMemory;
        uint32_t sphereIndexCount = 0;

        vk::Buffer capsuleVertexBuffer;
        vk::DeviceMemory capsuleVertexMemory;
        vk::Buffer capsuleIndexBuffer;
        vk::DeviceMemory capsuleIndexMemory;
        uint32_t capsuleIndexCount = 0;

        mutable std::unordered_map<std::string, MeshDebugData> meshCache;

        struct HeightFieldCacheEntry
        {
            MeshDebugData buffers;
            uint32_t version = 0;
        };
        mutable std::unordered_map<std::string, HeightFieldCacheEntry> heightfieldCache;

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

    private:
        void loadShader();
        void createPipeline(vk::RenderPass renderPass);
        void createBuffers();

        void createBoxBuffers();
        void createSphereBuffers();
        void createCapsuleBuffers();

        const MeshDebugData* getOrCreateMeshBuffers(const std::string& meshPath) const;
        void cleanupMeshCache();

        const MeshDebugData* getOrCreateHeightFieldBuffers(const PhysicsColliderRenderData& data) const;
        void cleanupHeightFieldCache();

        glm::vec4 getColorForCollider(const PhysicsColliderRenderData& data) const;
    };
}

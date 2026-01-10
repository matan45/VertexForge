#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>
#include <functional>
#include <string>

namespace core
{
    class Device;
    class SwapChain;
}

namespace render::mesh
{
    class AABBDebugRenderer;
    class FrustumDebugRenderer;
    class AudioSphereDebugRenderer;
    class GridRenderer;
    class PhysicsDebugRenderer;
    struct MeshRenderData;
    struct MeshGPUData;
    struct CameraFrustumRenderData;
    struct AudioSphereRenderData;
    struct PhysicsColliderRenderData;
}

namespace render
{
    class DebugRenderer
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        std::unique_ptr<mesh::AABBDebugRenderer> aabbRenderer;
        std::unique_ptr<mesh::FrustumDebugRenderer> frustumRenderer;
        std::unique_ptr<mesh::AudioSphereDebugRenderer> audioSphereRenderer;
        std::unique_ptr<mesh::GridRenderer> gridRenderer;
        std::unique_ptr<mesh::PhysicsDebugRenderer> physicsDebugRenderer;
        
        std::vector<mesh::CameraFrustumRenderData> cameraFrustumDrawList;
        
        std::vector<mesh::AudioSphereRenderData> audioSphereDrawList;
        
        std::vector<mesh::PhysicsColliderRenderData> physicsColliderDrawList;

        bool initialized = false;
        bool showGrid = true;
        bool showPhysicsDebug = false;
        bool hasBoundingBoxesToRender = false;

    public:
        explicit DebugRenderer(core::Device& device, core::SwapChain& swapChain);
        ~DebugRenderer();

        void init(vk::RenderPass renderPass);
        void recreate(vk::RenderPass renderPass);
        void cleanUp();
        void cleanUpShaders();

        void setCameraFrustumDrawList(std::vector<mesh::CameraFrustumRenderData>&& frustums);

        void setAudioSphereDrawList(std::vector<mesh::AudioSphereRenderData>&& spheres);

        void setPhysicsColliderDrawList(std::vector<mesh::PhysicsColliderRenderData>&& colliders);
        void setShowPhysicsDebug(bool show) { showPhysicsDebug = show; }
        bool getShowPhysicsDebug() const { return showPhysicsDebug; }

        void render(const vk::CommandBuffer& commandBuffer,
                    const std::vector<mesh::MeshRenderData>& meshDrawList,
                    const glm::mat4& view,
                    const glm::mat4& projection,
                    const std::function<const mesh::MeshGPUData*(const std::string&)>& getMeshFunc) const;

        bool isInitialized() const { return initialized; }

        bool hasItemsToRender() const;

        void setHasBoundingBoxes(bool hasBoundingBoxes) { hasBoundingBoxesToRender = hasBoundingBoxes; }
        
        void setShowGrid(bool show);
        bool getShowGrid() const { return showGrid; }
    };
}

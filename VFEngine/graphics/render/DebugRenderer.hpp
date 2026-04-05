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
    class LightGizmoDebugRenderer;
    class ClusterDebugRenderer;
    class ShadowDebugRenderer;
    class UICanvasDebugRenderer;
    class UICanvasImageRenderer;
    class NavmeshDebugRenderer;
    class ImmediateDebugRenderer;
    struct ImmediateDebugDrawList;
    struct MeshRenderData;
    struct MeshGPUData;
    struct CameraFrustumRenderData;
    struct AudioSphereRenderData;
    struct PhysicsColliderRenderData;
    struct LightGizmoRenderData;
    struct ClusterDebugRenderData;
    struct ShadowFrustumRenderData;
    struct UICanvasOutlineRenderData;
    struct UICanvasImageRenderData;
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
        std::unique_ptr<mesh::LightGizmoDebugRenderer> lightGizmoRenderer;
        std::unique_ptr<mesh::ClusterDebugRenderer> clusterDebugRenderer;
        std::unique_ptr<mesh::ShadowDebugRenderer> shadowDebugRenderer;
        std::unique_ptr<mesh::UICanvasDebugRenderer> uiCanvasRenderer;
        std::unique_ptr<mesh::UICanvasImageRenderer> uiCanvasImageRenderer;
        std::unique_ptr<mesh::NavmeshDebugRenderer> navmeshDebugRenderer;
        std::unique_ptr<mesh::ImmediateDebugRenderer> immediateDebugRenderer;

        std::vector<mesh::CameraFrustumRenderData> cameraFrustumDrawList;

        std::vector<mesh::AudioSphereRenderData> audioSphereDrawList;

        std::vector<mesh::PhysicsColliderRenderData> physicsColliderDrawList;

        std::vector<mesh::LightGizmoRenderData> lightGizmoDrawList;

        std::unique_ptr<mesh::ClusterDebugRenderData> clusterDebugData;

        std::vector<mesh::ShadowFrustumRenderData> shadowFrustumDrawList;

        std::vector<mesh::UICanvasOutlineRenderData> uiCanvasDrawList;
        std::vector<mesh::UICanvasImageRenderData> uiCanvasImageDrawList;

        bool initialized = false;
        bool showGrid = true;
        bool showPhysicsDebug = false;
        bool showClusterDebug = false;
        bool showShadowDebug = false;
        bool showNavmeshDebug = false;
        bool hasBoundingBoxesToRender = false;

    public:
        explicit DebugRenderer(core::Device& device, core::SwapChain& swapChain);
        ~DebugRenderer();

        void init(vk::Format colorFormat, vk::Format depthFormat);
        void recreate(vk::Format colorFormat, vk::Format depthFormat);
        void cleanUp();
        void cleanUpShaders();

        void setCameraFrustumDrawList(std::vector<mesh::CameraFrustumRenderData>&& frustums);

        void setAudioSphereDrawList(std::vector<mesh::AudioSphereRenderData>&& spheres);

        void setPhysicsColliderDrawList(std::vector<mesh::PhysicsColliderRenderData>&& colliders);
        void setShowPhysicsDebug(bool show) { showPhysicsDebug = show; }
        bool getShowPhysicsDebug() const { return showPhysicsDebug; }

        void setLightGizmoDrawList(std::vector<mesh::LightGizmoRenderData>&& gizmos);

        void setClusterDebugData(mesh::ClusterDebugRenderData&& data);
        void setShowClusterDebug(bool show);
        bool getShowClusterDebug() const { return showClusterDebug; }

        void setShadowFrustumDrawList(std::vector<mesh::ShadowFrustumRenderData>&& frustums);
        void setUICanvasOutlineDrawList(std::vector<mesh::UICanvasOutlineRenderData>&& outlines);
        void setUICanvasImageDrawList(std::vector<mesh::UICanvasImageRenderData>&& images);
        void setShowShadowDebug(bool show) { showShadowDebug = show; }
        bool getShowShadowDebug() const { return showShadowDebug; }

        void setShowNavmeshDebug(bool show) { showNavmeshDebug = show; }
        bool getShowNavmeshDebug() const { return showNavmeshDebug; }
        void updateNavmeshDebugMesh(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& indices);
        void clearNavmeshDebugMesh();

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

        void updateImmediateDebugDrawList(mesh::ImmediateDebugDrawList drawList);
    };
}

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
    struct MeshRenderData;
    struct MeshGPUData;
    struct CameraFrustumRenderData;
    struct AudioSphereRenderData;
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

        // Camera frustum draw list for current frame
        std::vector<mesh::CameraFrustumRenderData> cameraFrustumDrawList;

        // Audio sphere draw list for current frame
        std::vector<mesh::AudioSphereRenderData> audioSphereDrawList;

        bool initialized = false;
        bool showGrid = true;
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
        
        void render(const vk::CommandBuffer& commandBuffer,
                    const std::vector<mesh::MeshRenderData>& meshDrawList,
                    const glm::mat4& view,
                    const glm::mat4& projection,
                    const std::function<const mesh::MeshGPUData*(const std::string&)>& getMeshFunc) const;

        bool isInitialized() const { return initialized; }
        
        bool hasItemsToRender() const;
        
        void setHasBoundingBoxes(bool hasBoundingBoxes) { hasBoundingBoxesToRender = hasBoundingBoxes; }

        // Grid visibility control
        void setShowGrid(bool show);
        bool getShowGrid() const { return showGrid; }
        
    };
}

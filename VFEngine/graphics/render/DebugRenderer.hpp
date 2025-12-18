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
    struct MeshRenderData;
    struct MeshGPUData;
    struct CameraFrustumRenderData;
}

namespace render
{
    /**
     * Central debug visualization renderer.
     * Manages all debug rendering tools (AABB wireframes, camera frustums, etc.)
     */
    class DebugRenderer
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        std::unique_ptr<mesh::AABBDebugRenderer> aabbRenderer;
        std::unique_ptr<mesh::FrustumDebugRenderer> frustumRenderer;

        // Camera frustum draw list for current frame
        std::vector<mesh::CameraFrustumRenderData> cameraFrustumDrawList;

        bool initialized = false;

    public:
        DebugRenderer(core::Device& device, core::SwapChain& swapChain);
        ~DebugRenderer();

        void init(vk::RenderPass renderPass);
        void recreate(vk::RenderPass renderPass);
        void cleanUp();
        void cleanUpShaders();

        // Set camera frustum draw list for current frame
        void setCameraFrustumDrawList(std::vector<mesh::CameraFrustumRenderData>&& frustums);

        // Render all debug visualizations
        void render(const vk::CommandBuffer& commandBuffer,
                    const std::vector<mesh::MeshRenderData>& meshDrawList,
                    const glm::mat4& view,
                    const glm::mat4& projection,
                    const std::function<const mesh::MeshGPUData*(const std::string&)>& getMeshFunc) const;

        bool isInitialized() const { return initialized; }
    };
}

#pragma once

#include "../mesh/MeshTypes.hpp"
#include "../lighting/ClusterGridTypes.hpp"
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
    // Render data for cluster debug visualization
    struct ClusterDebugRenderData
    {
        std::vector<lighting::GPUClusterAABB> clusterAABBs;  // View-space AABBs
        glm::mat4 invViewMatrix{1.0f};                       // To transform view-space to world-space
        std::vector<uint32_t> highlightedClusterIndices;     // Clusters affected by selected light
        bool showAllClusters = false;                        // Show all clusters vs only highlighted
    };

    class ClusterDebugRenderer
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        std::shared_ptr<core::Shader> wireframeShader;

        vk::Pipeline wireframePipeline;
        vk::PipelineLayout wireframePipelineLayout;

        vk::Buffer vertexBuffer;
        vk::DeviceMemory vertexBufferMemory;
        vk::Buffer indexBuffer;
        vk::DeviceMemory indexBufferMemory;

        bool initialized = false;
        bool visible = false;

    public:
        explicit ClusterDebugRenderer(core::Device& device, core::SwapChain& swapChain);
        ~ClusterDebugRenderer();

        void init(vk::RenderPass renderPass);
        void recreate(vk::RenderPass renderPass);
        void cleanUp();
        void cleanUpShader();

        void render(const vk::CommandBuffer& commandBuffer,
                    const ClusterDebugRenderData& data,
                    const glm::mat4& view,
                    const glm::mat4& projection) const;

        void setVisible(bool show) { visible = show; }
        [[nodiscard]] bool isVisible() const { return visible; }
        [[nodiscard]] bool isInitialized() const { return initialized; }

    private:
        void loadShader();
        void createPipeline(vk::RenderPass renderPass);
        void createBuffers();

        void renderClusterAABB(const vk::CommandBuffer& commandBuffer,
                               const lighting::GPUClusterAABB& aabb,
                               const glm::mat4& invView,
                               const glm::mat4& viewProjection,
                               const glm::vec4& color) const;
    };
}

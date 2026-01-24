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

    // GPU instance data for instanced rendering
    struct ClusterInstance
    {
        glm::vec4 minPoint;   // xyz = view-space min, w = unused
        glm::vec4 maxPoint;   // xyz = view-space max, w = highlighted flag
        glm::vec4 color;      // RGBA color
    };

    // Push constants for cluster wireframe shader
    struct ClusterPushConstants
    {
        glm::mat4 viewProjection;
        glm::mat4 invViewMatrix;
    };

    class ClusterDebugRenderer
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        std::shared_ptr<core::Shader> wireframeShader;

        vk::Pipeline wireframePipeline;
        vk::PipelineLayout wireframePipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        // Unit cube geometry
        vk::Buffer vertexBuffer;
        vk::DeviceMemory vertexBufferMemory;
        vk::Buffer indexBuffer;
        vk::DeviceMemory indexBufferMemory;

        // Instance data storage buffer
        vk::Buffer instanceBuffer;
        vk::DeviceMemory instanceBufferMemory;
        void* instanceBufferMapped = nullptr;
        static constexpr uint32_t MAX_INSTANCES = lighting::ClusterConstants::MAX_CLUSTERS;

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
                    const glm::mat4& projection);

        void setVisible(bool show) { visible = show; }
        [[nodiscard]] bool isVisible() const { return visible; }
        [[nodiscard]] bool isInitialized() const { return initialized; }

    private:
        void loadShader();
        void createDescriptorSetLayout();
        void createPipeline(vk::RenderPass renderPass);
        void createBuffers();
        void createDescriptorPool();
        void createDescriptorSet();

        uint32_t uploadInstanceData(const ClusterDebugRenderData& data);
    };
}

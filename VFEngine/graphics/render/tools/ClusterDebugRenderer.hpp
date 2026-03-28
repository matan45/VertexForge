#pragma once

#include "DebugRendererBase.hpp"
#include "../mesh/MeshTypes.hpp"
#include "../lighting/ClusterGridTypes.hpp"
#include <memory>
#include <vector>

namespace core
{
    class Shader;
}

namespace render::mesh
{
    struct ClusterDebugRenderData
    {
        std::vector<lighting::GPUClusterAABB> clusterAABBs;
        glm::mat4 invViewMatrix{1.0f};
        std::vector<uint32_t> highlightedClusterIndices;
        bool showAllClusters = false;
    };

    struct ClusterInstance
    {
        glm::vec4 minPoint;
        glm::vec4 maxPoint;
        glm::vec4 color;
    };

    struct ClusterPushConstants
    {
        glm::mat4 viewProjection;
        glm::mat4 invViewMatrix;
    };

    class ClusterDebugRenderer : public DebugRendererBase
    {
    private:
        std::shared_ptr<core::Shader> wireframeShader;

        vk::Pipeline wireframePipeline;
        vk::PipelineLayout wireframePipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        // Unit cube geometry
        vk::Buffer vertexBuffer;
        core::VulkanAllocation vertexBufferAllocation;
        vk::Buffer indexBuffer;
        core::VulkanAllocation indexBufferAllocation;

        // Instance data storage buffer
        vk::Buffer instanceBuffer;
        core::VulkanAllocation instanceBufferAllocation;
        void* instanceBufferMapped = nullptr;
        static constexpr uint32_t MAX_INSTANCES = lighting::ClusterConstants::MAX_CLUSTERS;

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

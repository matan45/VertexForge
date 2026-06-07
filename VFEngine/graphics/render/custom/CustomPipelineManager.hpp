#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <vector>
#include "../../core/VulkanMemoryManager.hpp"
#include "../../../services/data/CustomPipelineTypes.hpp"

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
}

namespace render::custom
{
    // Descriptor set layouts shared by lit custom pipelines (receiveLighting).
    // Non-owning — each layout is owned by its engine-side manager. Slots map to
    // shader set numbers: ibl=0, lights=6, clusterParams=7, clusterIndices=8,
    // shadowData=9, shadowTextures=10 (sets 1-5 are empty placeholders).
    struct CustomLightingLayouts
    {
        vk::DescriptorSetLayout ibl;
        vk::DescriptorSetLayout lights;
        vk::DescriptorSetLayout clusterParams;
        vk::DescriptorSetLayout clusterIndices;
        vk::DescriptorSetLayout shadowData;
        vk::DescriptorSetLayout shadowTextures;

        [[nodiscard]] bool isComplete() const
        {
            return ibl && lights && clusterParams && clusterIndices && shadowData && shadowTextures;
        }
    };

    // Per-frame descriptor sets bound to lit custom pipelines; same slot -> set
    // mapping as CustomLightingLayouts. Lit draws are skipped while any set is null.
    struct CustomLightingSets
    {
        vk::DescriptorSet ibl;
        vk::DescriptorSet lights;
        vk::DescriptorSet clusterParams;
        vk::DescriptorSet clusterIndices;
        vk::DescriptorSet shadowData;
        vk::DescriptorSet shadowTextures;

        [[nodiscard]] bool isComplete() const
        {
            return ibl && lights && clusterParams && clusterIndices && shadowData && shadowTextures;
        }
    };

    // Engine-side owner of plugin-created graphics pipelines and meshes.
    // Plugins describe pipelines (GLSL source + fixed-function state) and geometry,
    // and receive opaque handles back; every Vulkan object lives here so no Vulkan
    // call ever crosses the plugin DLL boundary (plugin DLLs have no initialized
    // VULKAN_HPP_DEFAULT_DISPATCHER).
    class CustomPipelineManager
    {
    private:
        struct PipelineEntry
        {
            plugin::CustomPipelineDesc desc;
            std::shared_ptr<core::Shader> shader;
            vk::Pipeline pipeline;
            vk::PipelineLayout layout;
        };

        struct MeshEntry
        {
            vk::Buffer vertexBuffer;
            core::VulkanAllocation vertexAllocation;
            vk::Buffer indexBuffer;
            core::VulkanAllocation indexAllocation;
            uint32_t indexCount = 0;
            uint32_t vertexCount = 0;
        };

        core::Device& device;
        core::SwapChain& swapChain;

        std::unordered_map<uint64_t, PipelineEntry> pipelines;
        std::unordered_map<uint64_t, MeshEntry> meshes;
        std::vector<plugin::CustomDrawItem> pendingDraws;
        uint64_t nextId = 1;

        // Lighting layouts for lit pipelines (set after the gpu-driven renderer
        // initializes). Lit pipelines created earlier are built lazily when the
        // layouts arrive. emptyLayout fills placeholder sets 1-5; owned here.
        CustomLightingLayouts lightingLayouts;
        vk::DescriptorSetLayout emptyLayout;

    public:
        explicit CustomPipelineManager(core::Device& device, core::SwapChain& swapChain);
        ~CustomPipelineManager();

        CustomPipelineManager(const CustomPipelineManager&) = delete;
        CustomPipelineManager& operator=(const CustomPipelineManager&) = delete;

        plugin::CustomPipelineHandle createPipeline(const plugin::CustomPipelineDesc& desc);
        plugin::CustomMeshHandle uploadMesh(plugin::CustomMeshData&& data);
        void enqueueDraw(plugin::CustomDrawItem&& item);
        void destroyPipeline(plugin::CustomPipelineHandle handle);
        void destroyMesh(plugin::CustomMeshHandle handle);

        [[nodiscard]] bool hasDraws() const { return !pendingDraws.empty(); }

        // Provides the engine lighting descriptor set layouts used by lit
        // pipelines (receiveLighting). Builds any lit pipelines whose creation
        // was deferred because the layouts were not yet available.
        void setLightingLayouts(const CustomLightingLayouts& layouts);

        // Records pending draws. Must be called inside an active dynamic-rendering
        // pass targeting the scene color + depth attachments (viewport/scissor set).
        // lightingSets feed lit pipelines; lit draws are skipped while incomplete.
        void render(const vk::CommandBuffer& commandBuffer,
                    const glm::mat4& view, const glm::mat4& projection,
                    const CustomLightingSets& lightingSets) const;

        // Drops this frame's pending draws — called once per frame after recording.
        void endFrame();

        // Swapchain/format change: rebuild all pipelines against the new formats.
        void recreatePipelines();

        void cleanUp();

    private:
        bool buildPipeline(PipelineEntry& entry);
        void destroyPipelineObjects(PipelineEntry& entry);
        void destroyMeshBuffers(MeshEntry& entry);

        static uint32_t attributeSize(plugin::CustomVertexAttribute attribute);
        static vk::Format attributeFormat(plugin::CustomVertexAttribute attribute);
    };
}

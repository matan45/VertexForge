#pragma once
#include "../core/OffScreen.hpp"
#include <memory>
#include <vector>

namespace core
{
    class Device;
    class SwapChain;
}

namespace render
{
    class ClearColor;
    class IBL;

    namespace mesh
    {
        class StaticMeshPipeline;
        struct MeshRenderData;
    }

    class RenderPassHandler
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        std::unique_ptr<ClearColor> clearColor;
        std::unique_ptr<IBL> iblRenderer;
        std::unique_ptr<mesh::StaticMeshPipeline> meshPipeline;

        core::OffscreenResources& offscreenResources;

        // Mesh rendering state
        bool meshPipelineInitialized = false;
        mutable std::vector<mesh::MeshRenderData> currentMeshDrawList;

    public:
        explicit RenderPassHandler(core::Device& device, core::SwapChain& swapChain,
                                   core::OffscreenResources& offscreenResources);
        ~RenderPassHandler();

        void init();

        void recreate() const;

        IBL* getIBL() const { return iblRenderer.get(); }

        // Mesh pipeline access
        mesh::StaticMeshPipeline* getMeshPipeline() const { return meshPipeline.get(); }
        bool isMeshPipelineInitialized() const { return meshPipelineInitialized; }

        // Initialize mesh pipeline (called when IBL is ready with textures)
        void initMeshPipeline();

        // Reinitialize mesh pipeline with default textures (called when IBL is removed)
        void reinitMeshPipelineWithDefaults();

        // Reinitialize mesh pipeline with IBL textures (called when IBL is set/changed)
        void reinitMeshPipelineWithIBL();

        // Set mesh draw list for the current frame
        void setMeshDrawList(std::vector<mesh::MeshRenderData> meshes);

        void cleanUp() const;

        void draw(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
    };
}

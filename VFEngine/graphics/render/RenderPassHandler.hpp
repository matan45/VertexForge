#pragma once
#include "../core/OffScreen.hpp"
#include "math/Frustum.hpp"
#include <glm/glm.hpp>
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

    namespace billboard
    {
        class BillboardPipeline;
        struct BillboardRenderData;
    }

    class RenderPassHandler
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        std::unique_ptr<ClearColor> clearColor;
        std::unique_ptr<IBL> iblRenderer;
        std::unique_ptr<mesh::StaticMeshPipeline> meshPipeline;
        std::unique_ptr<billboard::BillboardPipeline> billboardPipeline;

        core::OffscreenResources& offscreenResources;

        // Mesh rendering state
        bool meshPipelineInitialized = false;
        mutable std::vector<mesh::MeshRenderData> currentMeshDrawList;
        const math::Frustum* currentFrustum = nullptr;

        // Billboard rendering state
        bool billboardPipelineInitialized = false;
        mutable std::vector<billboard::BillboardRenderData> currentBillboardDrawList;

    public:
        explicit RenderPassHandler(core::Device& device, core::SwapChain& swapChain,
                                   core::OffscreenResources& offscreenResources);
        ~RenderPassHandler();

        void init();

        void recreate() const;

        IBL* getIBL() const { return iblRenderer.get(); }
        
        mesh::StaticMeshPipeline* getMeshPipeline() const { return meshPipeline.get(); }
        bool isMeshPipelineInitialized() const { return meshPipelineInitialized; }
        
        void initMeshPipeline();

        // (called when IBL is removed)
        void reinitMeshPipelineWithDefaults();

        //(called when IBL is set/changed)
        void reinitMeshPipelineWithIBL();
        
        void setMeshDrawList(const std::vector<mesh::MeshRenderData>& meshes);
        void setCurrentFrustum(const math::Frustum* frustum) { currentFrustum = frustum; }

        // Billboard pipeline methods
        void initBillboardPipeline();
        billboard::BillboardPipeline* getBillboardPipeline() const { return billboardPipeline.get(); }
        bool isBillboardPipelineInitialized() const { return billboardPipelineInitialized; }
        void setBillboardDrawList(std::vector<billboard::BillboardRenderData>&& billboards);

        void cleanUp() const;

        void draw(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
    };
}

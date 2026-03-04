#pragma once
#include "../../core/OffScreen.hpp"
#include "math/Frustum.hpp"
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
}

namespace render::preview
{
    class PreviewRenderHandler
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        std::unique_ptr<ClearColor> clearColor;
        std::unique_ptr<IBL> iblRenderer;
        std::unique_ptr<mesh::StaticMeshPipeline> meshPipeline;

        core::OffscreenResources& offscreenResources;

        bool meshPipelineInitialized = false;
        std::vector<mesh::MeshRenderData> currentMeshDrawList;
        const math::Frustum* currentFrustum = nullptr;

    public:
        explicit PreviewRenderHandler(core::Device& device, core::SwapChain& swapChain,
                                       core::OffscreenResources& offscreenResources);
        ~PreviewRenderHandler();

        void init();
        void recreate();
        void cleanUp() const;

        IBL* getIBL() const { return iblRenderer.get(); }
        mesh::StaticMeshPipeline* getMeshPipeline() const { return meshPipeline.get(); }
        bool isMeshPipelineInitialized() const { return meshPipelineInitialized; }

        void initMeshPipeline();
        void reinitMeshPipelineWithDefaults();
        void reinitMeshPipelineWithIBL();

        void setMeshDrawList(std::vector<mesh::MeshRenderData>&& meshes);
        void setCurrentFrustum(const math::Frustum* frustum) { currentFrustum = frustum; }

        void draw(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
    };
}

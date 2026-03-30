#pragma once
#include "../../core/OffScreen.hpp"
#include "../../../services/providers/render/IMeshPreviewProvider.hpp"
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
}

namespace render::preview
{
    class PreviewBackgroundRenderer;
    class PreviewGridRenderer;

    class PreviewRenderHandler
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        std::unique_ptr<ClearColor> clearColor;
        std::unique_ptr<IBL> iblRenderer;
        std::unique_ptr<mesh::StaticMeshPipeline> meshPipeline;
        std::unique_ptr<PreviewBackgroundRenderer> backgroundRenderer;
        std::unique_ptr<PreviewGridRenderer> previewGrid;

        core::OffscreenResources& offscreenResources;

        bool meshPipelineInitialized = false;
        bool gridInitialized = false;
        bool backgroundInitialized = false;
        std::vector<mesh::MeshRenderData> currentMeshDrawList;
        const math::Frustum* currentFrustum = nullptr;
        services::PreviewEnvironmentParams envParams;

        // Camera matrices cached for grid rendering
        glm::mat4 cachedView{1.0f};
        glm::mat4 cachedProjection{1.0f};

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
        void setEnvironmentParams(const services::PreviewEnvironmentParams& params) { envParams = params; }
        void setCachedCameraMatrices(const glm::mat4& view, const glm::mat4& projection)
        {
            cachedView = view;
            cachedProjection = projection;
        }

        void draw(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
    };
}

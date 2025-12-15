#pragma once
#include "../interfaces/IPreviewService.hpp"
#include "../events/PreviewEvents.hpp"

namespace services {

    class IPreviewProvider;

    /**
     * @brief Implementation of IPreviewService using provider abstraction.
     *
     * This class delegates preview operations to an IPreviewProvider,
     * which is implemented by Core/Graphics adapters.
     *
     * Supports multiple instances via instanceId parameter - each instance
     * gets its own independent preview (e.g., for multiple editor windows).
     */
    class PreviewServiceImpl : public IPreviewService {
    public:
        /**
         * @brief Construct with preview provider.
         * @param provider Provider for preview operations (required)
         */
        explicit PreviewServiceImpl(IPreviewProvider* provider);
        ~PreviewServiceImpl() override;

        /**
         * @brief Register event handlers for CQRS pattern.
         */
        void registerEventHandlers();

        // === Material Preview (IPreviewService) ===
        void initMaterialPreview(void* instanceId) override;
        void cleanUpMaterialPreview(void* instanceId) override;
        bool isMaterialPreviewReady(void* instanceId) const override;
        void setMaterialParams(void* instanceId, const MaterialPreviewParams& params) override;
        MaterialPreviewParams getMaterialParams(void* instanceId) const override;
        void updateMaterialCamera(void* instanceId, const glm::mat4& view, const glm::mat4& projection,
                                  const glm::vec3& cameraPos, float time = 0.0f) override;
        ViewportTextureHandle renderMaterialPreview(void* instanceId) override;
        std::string getMaterialShaderError(void* instanceId) const override;

        // === Mesh Preview (IPreviewService) ===
        void initMeshPreview(void* instanceId) override;
        void cleanUpMeshPreview(void* instanceId) override;
        bool isMeshPreviewReady(void* instanceId) const override;
        bool loadPreviewMesh(void* instanceId, const std::string& meshPath, math::AABB& outBounds) override;
        void unloadPreviewMesh(void* instanceId) override;
        bool isPreviewMeshLoaded(void* instanceId) const override;
        std::vector<SubMeshInfo> getPreviewMeshSubMeshInfo(void* instanceId) const override;
        math::AABB getPreviewMeshBounds(void* instanceId) const override;
        void setMeshPreviewParams(void* instanceId, const MeshPreviewParams& params) override;
        void updateMeshCamera(void* instanceId, const glm::mat4& view, const glm::mat4& projection,
                               const glm::vec3& cameraPos) override;
        ViewportTextureHandle renderMeshPreview(void* instanceId) override;

    private:
        IPreviewProvider* provider;
    };

}

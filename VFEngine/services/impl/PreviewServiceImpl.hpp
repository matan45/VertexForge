#pragma once
#include "../interfaces/IPreviewService.hpp"
#include "../events/PreviewEvents.hpp"

namespace services {

    class IMaterialPreviewProvider;
    class IMeshPreviewProvider;

    /**
     * @brief Implementation of IPreviewService using provider abstraction.
     *
     * This class delegates preview operations to separate providers:
     * - IMaterialPreviewProvider for material previews
     * - IMeshPreviewProvider for mesh previews
     *
     * Supports multiple instances via instanceId parameter - each instance
     * gets its own independent preview (e.g., for multiple editor windows).
     *
     * @note Providers must not be null - this is enforced via assertion.
     *       A null provider indicates a programming error during bootstrap.
     */
    class PreviewServiceImpl : public IPreviewService {
    public:
        /**
         * @brief Construct with separate preview providers.
         * @param materialProvider Provider for material preview operations (must not be null)
         * @param meshProvider Provider for mesh preview operations (must not be null)
         * @pre materialProvider != nullptr && meshProvider != nullptr
         */
        explicit PreviewServiceImpl(IMaterialPreviewProvider* materialProvider, IMeshPreviewProvider* meshProvider);
        ~PreviewServiceImpl() override;

        /**
         * @brief Register event handlers for CQRS pattern.
         */
        void registerEventHandlers() override;

        // === Material Preview (IPreviewService) ===
        void initMaterialPreview(PreviewInstanceId instanceId) override;
        void cleanUpMaterialPreview(PreviewInstanceId instanceId) override;
        void setMaterialParams(PreviewInstanceId instanceId, const MaterialPreviewParams& params) override;
        void updateMaterialCamera(PreviewInstanceId instanceId, const glm::mat4& view, const glm::mat4& projection,
                                  const glm::vec3& cameraPos, float time = 0.0f) override;
        [[nodiscard]] ViewportTextureHandle renderMaterialPreview(PreviewInstanceId instanceId) override;
        [[nodiscard]] std::string getMaterialShaderError(PreviewInstanceId instanceId) const override;

        // === Mesh Preview (IPreviewService) ===
        void initMeshPreview(PreviewInstanceId instanceId) override;
        void cleanUpMeshPreview(PreviewInstanceId instanceId) override;
        [[nodiscard]] std::vector<SubMeshInfo> getPreviewMeshSubMeshInfo(PreviewInstanceId instanceId) const override;
        [[nodiscard]] std::vector<LODInfo> getPreviewMeshLODInfo(PreviewInstanceId instanceId) const override;
        [[nodiscard]] math::AABB getPreviewMeshBounds(PreviewInstanceId instanceId) const override;
        void setMeshPreviewParams(PreviewInstanceId instanceId, const MeshPreviewParams& params) override;
        void updateMeshCamera(PreviewInstanceId instanceId, const glm::mat4& view, const glm::mat4& projection,
                               const glm::vec3& cameraPos) override;
        [[nodiscard]] ViewportTextureHandle renderMeshPreview(PreviewInstanceId instanceId) override;

        // Async mesh loading
        void loadPreviewMeshAsync(PreviewInstanceId instanceId, const std::string& meshPath) override;
        void cancelMeshLoading(PreviewInstanceId instanceId) override;
        [[nodiscard]] MeshLoadingProgress getMeshLoadingProgress(PreviewInstanceId instanceId) const override;
        void processAsyncLoading() override;

    private:
        IMaterialPreviewProvider* materialProvider;
        IMeshPreviewProvider* meshProvider;
    };

}

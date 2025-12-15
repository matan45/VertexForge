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
     *
     * @note Provider must not be null - this is enforced via assertion.
     *       A null provider indicates a programming error during bootstrap.
     */
    class PreviewServiceImpl : public IPreviewService {
    public:
        /**
         * @brief Construct with preview provider.
         * @param provider Provider for preview operations (must not be null)
         * @pre provider != nullptr
         */
        explicit PreviewServiceImpl(IPreviewProvider* provider);
        ~PreviewServiceImpl() override;

        /**
         * @brief Register event handlers for CQRS pattern.
         */
        void registerEventHandlers();

        // === Material Preview (IPreviewService) ===
        void initMaterialPreview(PreviewInstanceId instanceId) override;
        void cleanUpMaterialPreview(PreviewInstanceId instanceId) override;
        [[nodiscard]] bool isMaterialPreviewReady(PreviewInstanceId instanceId) const override;
        void setMaterialParams(PreviewInstanceId instanceId, const MaterialPreviewParams& params) override;
        [[nodiscard]] MaterialPreviewParams getMaterialParams(PreviewInstanceId instanceId) const override;
        void updateMaterialCamera(PreviewInstanceId instanceId, const glm::mat4& view, const glm::mat4& projection,
                                  const glm::vec3& cameraPos, float time = 0.0f) override;
        [[nodiscard]] ViewportTextureHandle renderMaterialPreview(PreviewInstanceId instanceId) override;
        [[nodiscard]] std::string getMaterialShaderError(PreviewInstanceId instanceId) const override;

        // === Mesh Preview (IPreviewService) ===
        void initMeshPreview(PreviewInstanceId instanceId) override;
        void cleanUpMeshPreview(PreviewInstanceId instanceId) override;
        [[nodiscard]] bool isMeshPreviewReady(PreviewInstanceId instanceId) const override;
        [[nodiscard]] bool loadPreviewMesh(PreviewInstanceId instanceId, const std::string& meshPath, math::AABB& outBounds) override;
        void unloadPreviewMesh(PreviewInstanceId instanceId) override;
        [[nodiscard]] bool isPreviewMeshLoaded(PreviewInstanceId instanceId) const override;
        [[nodiscard]] std::vector<SubMeshInfo> getPreviewMeshSubMeshInfo(PreviewInstanceId instanceId) const override;
        [[nodiscard]] math::AABB getPreviewMeshBounds(PreviewInstanceId instanceId) const override;
        void setMeshPreviewParams(PreviewInstanceId instanceId, const MeshPreviewParams& params) override;
        void updateMeshCamera(PreviewInstanceId instanceId, const glm::mat4& view, const glm::mat4& projection,
                               const glm::vec3& cameraPos) override;
        [[nodiscard]] ViewportTextureHandle renderMeshPreview(PreviewInstanceId instanceId) override;

    private:
        IPreviewProvider* provider;
    };

}

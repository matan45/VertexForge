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
        void initMaterialPreview(void* instanceId) override;
        void cleanUpMaterialPreview(void* instanceId) override;
        [[nodiscard]] bool isMaterialPreviewReady(void* instanceId) const override;
        void setMaterialParams(void* instanceId, const MaterialPreviewParams& params) override;
        [[nodiscard]] MaterialPreviewParams getMaterialParams(void* instanceId) const override;
        void updateMaterialCamera(void* instanceId, const glm::mat4& view, const glm::mat4& projection,
                                  const glm::vec3& cameraPos, float time = 0.0f) override;
        [[nodiscard]] ViewportTextureHandle renderMaterialPreview(void* instanceId) override;
        [[nodiscard]] std::string getMaterialShaderError(void* instanceId) const override;

        // === Mesh Preview (IPreviewService) ===
        void initMeshPreview(void* instanceId) override;
        void cleanUpMeshPreview(void* instanceId) override;
        [[nodiscard]] bool isMeshPreviewReady(void* instanceId) const override;
        [[nodiscard]] bool loadPreviewMesh(void* instanceId, const std::string& meshPath, math::AABB& outBounds) override;
        void unloadPreviewMesh(void* instanceId) override;
        [[nodiscard]] bool isPreviewMeshLoaded(void* instanceId) const override;
        [[nodiscard]] std::vector<SubMeshInfo> getPreviewMeshSubMeshInfo(void* instanceId) const override;
        [[nodiscard]] math::AABB getPreviewMeshBounds(void* instanceId) const override;
        void setMeshPreviewParams(void* instanceId, const MeshPreviewParams& params) override;
        void updateMeshCamera(void* instanceId, const glm::mat4& view, const glm::mat4& projection,
                               const glm::vec3& cameraPos) override;
        [[nodiscard]] ViewportTextureHandle renderMeshPreview(void* instanceId) override;

    private:
        IPreviewProvider* provider;
    };

}

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
        void initMaterialPreview() override;
        void cleanUpMaterialPreview() override;
        bool isMaterialPreviewReady() const override;
        void setMaterialParams(const MaterialPreviewParams& params) override;
        MaterialPreviewParams getMaterialParams() const override;
        void updateMaterialCamera(const glm::mat4& view, const glm::mat4& projection,
                                  const glm::vec3& cameraPos, float time = 0.0f) override;
        ViewportTextureHandle renderMaterialPreview() override;
        std::string getMaterialShaderError() const override;

        // === Mesh Preview (IPreviewService) ===
        void initMeshPreview() override;
        void cleanUpMeshPreview() override;
        bool isMeshPreviewReady() const override;
        bool loadPreviewMesh(const std::string& meshPath, math::AABB& outBounds) override;
        void unloadPreviewMesh() override;
        bool isPreviewMeshLoaded() const override;
        std::vector<SubMeshInfo> getPreviewMeshSubMeshInfo() const override;
        math::AABB getPreviewMeshBounds() const override;
        void setMeshPreviewParams(const MeshPreviewParams& params) override;
        void updateMeshCamera(const glm::mat4& view, const glm::mat4& projection,
                               const glm::vec3& cameraPos) override;
        ViewportTextureHandle renderMeshPreview() override;

    private:
        IPreviewProvider* provider;
    };

}

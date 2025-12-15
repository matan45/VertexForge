#pragma once
#include "../../services/providers/IPreviewProvider.hpp"
#include <memory>

namespace controllers {
    class MaterialPreviewController;
    class MeshPreviewController;
}

namespace core {

    /**
     * @brief Adapter that implements IPreviewProvider by wrapping preview controllers.
     *
     * This class bridges the Services layer with the Graphics layer's preview controllers,
     * allowing Services to provide preview functionality without direct dependencies on Graphics.
     */
    class PreviewAdapter : public services::IPreviewProvider {
    public:
        PreviewAdapter();
        ~PreviewAdapter() override;

        // === Material Preview (IPreviewProvider) ===
        void initMaterialPreview() override;
        void cleanUpMaterialPreview() override;
        bool isMaterialPreviewInitialized() const override;
        void setMaterialParams(const services::MaterialPreviewParams& params) override;
        services::MaterialPreviewParams getMaterialParams() const override;
        void updateMaterialCamera(const glm::mat4& view, const glm::mat4& projection,
                                  const glm::vec3& cameraPos, float time = 0.0f) override;
        void* renderMaterialPreview() override;
        std::string getMaterialShaderError() const override;

        // === Mesh Preview (IPreviewProvider) ===
        void initMeshPreview() override;
        void cleanUpMeshPreview() override;
        bool isMeshPreviewInitialized() const override;
        bool loadPreviewMesh(const std::string& meshPath, math::AABB& outBounds) override;
        void unloadPreviewMesh() override;
        bool isPreviewMeshLoaded() const override;
        std::vector<services::SubMeshInfo> getPreviewMeshSubMeshInfo() const override;
        math::AABB getPreviewMeshBounds() const override;
        void setMeshPreviewParams(const services::MeshPreviewParams& params) override;
        void updateMeshCamera(const glm::mat4& view, const glm::mat4& projection,
                               const glm::vec3& cameraPos) override;
        void* renderMeshPreview() override;

    private:
        std::unique_ptr<::controllers::MaterialPreviewController> materialController;
        std::unique_ptr<::controllers::MeshPreviewController> meshController;
    };

}

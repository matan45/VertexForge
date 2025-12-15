#include "PreviewAdapter.hpp"
#include "../../graphics/controllers/MaterialPreviewController.hpp"
#include "../../graphics/controllers/MeshPreviewController.hpp"

namespace core {

    PreviewAdapter::PreviewAdapter()
        : materialController(std::make_unique<::controllers::MaterialPreviewController>())
        , meshController(std::make_unique<::controllers::MeshPreviewController>()) {}

    PreviewAdapter::~PreviewAdapter() {
        cleanUpMaterialPreview();
        cleanUpMeshPreview();
    }

    // === Material Preview ===

    void PreviewAdapter::initMaterialPreview() {
        if (materialController) {
            materialController->init();
        }
    }

    void PreviewAdapter::cleanUpMaterialPreview() {
        if (materialController) {
            materialController->cleanUp();
        }
    }

    bool PreviewAdapter::isMaterialPreviewInitialized() const {
        return materialController && materialController->isInitialized();
    }

    void PreviewAdapter::setMaterialParams(const services::MaterialPreviewParams& params) {
        if (!materialController) return;

        // Convert service DTO to controller params
        ::controllers::PreviewMaterialParams controllerParams;
        controllerParams.albedo = params.albedo;
        controllerParams.metallic = params.metallic;
        controllerParams.roughness = params.roughness;
        controllerParams.ao = params.ao;
        controllerParams.emission = params.emission;
        controllerParams.albedoTexturePath = params.albedoTexturePath;
        controllerParams.metallicTexturePath = params.metallicTexturePath;
        controllerParams.roughnessTexturePath = params.roughnessTexturePath;
        controllerParams.aoTexturePath = params.aoTexturePath;
        controllerParams.normalTexturePath = params.normalTexturePath;
        controllerParams.emissionTexturePath = params.emissionTexturePath;
        controllerParams.materialPath = params.materialPath;
        controllerParams.useCustomShader = params.useCustomShader;

        // Handle material data if provided
        if (params.materialDataHandle) {
            controllerParams.materialData =
                *static_cast<std::shared_ptr<material::MaterialData>*>(params.materialDataHandle);
        }

        materialController->setMaterialParams(controllerParams);
    }

    services::MaterialPreviewParams PreviewAdapter::getMaterialParams() const {
        services::MaterialPreviewParams result;
        if (!materialController) return result;

        const auto& controllerParams = materialController->getMaterialParams();
        result.albedo = controllerParams.albedo;
        result.metallic = controllerParams.metallic;
        result.roughness = controllerParams.roughness;
        result.ao = controllerParams.ao;
        result.emission = controllerParams.emission;
        result.albedoTexturePath = controllerParams.albedoTexturePath;
        result.metallicTexturePath = controllerParams.metallicTexturePath;
        result.roughnessTexturePath = controllerParams.roughnessTexturePath;
        result.aoTexturePath = controllerParams.aoTexturePath;
        result.normalTexturePath = controllerParams.normalTexturePath;
        result.emissionTexturePath = controllerParams.emissionTexturePath;
        result.materialPath = controllerParams.materialPath;
        result.useCustomShader = controllerParams.useCustomShader;
        // Note: materialDataHandle is not copied back - it's write-only from service perspective

        return result;
    }

    void PreviewAdapter::updateMaterialCamera(const glm::mat4& view, const glm::mat4& projection,
                                               const glm::vec3& cameraPos, float time) {
        if (materialController) {
            materialController->updateCamera(view, projection, cameraPos, time);
        }
    }

    void* PreviewAdapter::renderMaterialPreview() {
        return materialController ? materialController->render() : nullptr;
    }

    std::string PreviewAdapter::getMaterialShaderError() const {
        return materialController ? materialController->getLastShaderCompilationError() : "";
    }

    // === Mesh Preview ===

    void PreviewAdapter::initMeshPreview() {
        if (meshController) {
            meshController->init();
        }
    }

    void PreviewAdapter::cleanUpMeshPreview() {
        if (meshController) {
            meshController->cleanUp();
        }
    }

    bool PreviewAdapter::isMeshPreviewInitialized() const {
        // MeshPreviewController doesn't have isInitialized, check if loadMesh would work
        return meshController != nullptr;
    }

    bool PreviewAdapter::loadPreviewMesh(const std::string& meshPath, math::AABB& outBounds) {
        return meshController && meshController->loadMesh(meshPath, outBounds);
    }

    void PreviewAdapter::unloadPreviewMesh() {
        if (meshController) {
            meshController->unloadMesh();
        }
    }

    bool PreviewAdapter::isPreviewMeshLoaded() const {
        return meshController && meshController->isMeshLoaded();
    }

    std::vector<services::SubMeshInfo> PreviewAdapter::getPreviewMeshSubMeshInfo() const {
        if (!meshController) return {};
        return meshController->getSubMeshInfo();
    }

    math::AABB PreviewAdapter::getPreviewMeshBounds() const {
        if (!meshController) return math::AABB{};
        return meshController->getMeshBounds();
    }

    void PreviewAdapter::setMeshPreviewParams(const services::MeshPreviewParams& params) {
        if (!meshController) return;
        meshController->setModelMatrix(params.modelMatrix);
        meshController->setHighlightedSubMesh(params.highlightedSubMesh);
    }

    void PreviewAdapter::updateMeshCamera(const glm::mat4& view, const glm::mat4& projection,
                                           const glm::vec3& cameraPos) {
        if (meshController) {
            meshController->updateCamera(view, projection, cameraPos);
        }
    }

    void* PreviewAdapter::renderMeshPreview() {
        return meshController ? meshController->render() : nullptr;
    }

}

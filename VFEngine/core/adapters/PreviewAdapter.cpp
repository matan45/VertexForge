#include "PreviewAdapter.hpp"
#include "../../graphics/controllers/MaterialPreviewController.hpp"
#include "../../graphics/controllers/MeshPreviewController.hpp"

namespace core {

    PreviewAdapter::PreviewAdapter() = default;

    PreviewAdapter::~PreviewAdapter() {
        // Clean up all material controllers
        for (auto& [id, controller] : materialControllers) {
            if (controller) {
                controller->cleanUp();
            }
        }
        materialControllers.clear();

        // Clean up all mesh controllers
        for (auto& [id, controller] : meshControllers) {
            if (controller) {
                controller->cleanUp();
            }
        }
        meshControllers.clear();
    }

    // === Helper Methods ===

    ::controllers::MaterialPreviewController* PreviewAdapter::getMaterialController(void* instanceId) {
        auto it = materialControllers.find(instanceId);
        if (it != materialControllers.end()) {
            return it->second.get();
        }
        return nullptr;
    }

    ::controllers::MaterialPreviewController* PreviewAdapter::getMaterialControllerConst(void* instanceId) const {
        auto it = materialControllers.find(instanceId);
        if (it != materialControllers.end()) {
            return it->second.get();
        }
        return nullptr;
    }

    ::controllers::MeshPreviewController* PreviewAdapter::getMeshController(void* instanceId) {
        auto it = meshControllers.find(instanceId);
        if (it != meshControllers.end()) {
            return it->second.get();
        }
        return nullptr;
    }

    ::controllers::MeshPreviewController* PreviewAdapter::getMeshControllerConst(void* instanceId) const {
        auto it = meshControllers.find(instanceId);
        if (it != meshControllers.end()) {
            return it->second.get();
        }
        return nullptr;
    }

    // === Material Preview ===

    void PreviewAdapter::initMaterialPreview(void* instanceId) {
        // Create a new controller for this instance if it doesn't exist
        auto& controller = materialControllers[instanceId];
        if (!controller) {
            controller = std::make_unique<::controllers::MaterialPreviewController>();
        }
        controller->init();
    }

    void PreviewAdapter::cleanUpMaterialPreview(void* instanceId) {
        auto it = materialControllers.find(instanceId);
        if (it != materialControllers.end()) {
            if (it->second) {
                it->second->cleanUp();
            }
            materialControllers.erase(it);
        }
    }

    bool PreviewAdapter::isMaterialPreviewInitialized(void* instanceId) const {
        auto* controller = getMaterialControllerConst(instanceId);
        return controller && controller->isInitialized();
    }

    void PreviewAdapter::setMaterialParams(void* instanceId, const services::MaterialPreviewParams& params) {
        auto* controller = getMaterialController(instanceId);
        if (!controller) return;

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

        controller->setMaterialParams(controllerParams);
    }

    services::MaterialPreviewParams PreviewAdapter::getMaterialParams(void* instanceId) const {
        services::MaterialPreviewParams result;
        auto* controller = getMaterialControllerConst(instanceId);
        if (!controller) return result;

        const auto& controllerParams = controller->getMaterialParams();
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

    void PreviewAdapter::updateMaterialCamera(void* instanceId, const glm::mat4& view, const glm::mat4& projection,
                                               const glm::vec3& cameraPos, float time) {
        auto* controller = getMaterialController(instanceId);
        if (controller) {
            controller->updateCamera(view, projection, cameraPos, time);
        }
    }

    void* PreviewAdapter::renderMaterialPreview(void* instanceId) {
        auto* controller = getMaterialController(instanceId);
        return controller ? controller->render() : nullptr;
    }

    std::string PreviewAdapter::getMaterialShaderError(void* instanceId) const {
        auto* controller = getMaterialControllerConst(instanceId);
        return controller ? controller->getLastShaderCompilationError() : "";
    }

    // === Mesh Preview ===

    void PreviewAdapter::initMeshPreview(void* instanceId) {
        // Create a new controller for this instance if it doesn't exist
        auto& controller = meshControllers[instanceId];
        if (!controller) {
            controller = std::make_unique<::controllers::MeshPreviewController>();
        }
        controller->init();
    }

    void PreviewAdapter::cleanUpMeshPreview(void* instanceId) {
        auto it = meshControllers.find(instanceId);
        if (it != meshControllers.end()) {
            if (it->second) {
                it->second->cleanUp();
            }
            meshControllers.erase(it);
        }
    }

    bool PreviewAdapter::isMeshPreviewInitialized(void* instanceId) const {
        auto* controller = getMeshControllerConst(instanceId);
        // MeshPreviewController doesn't have isInitialized, check if controller exists
        return controller != nullptr;
    }

    bool PreviewAdapter::loadPreviewMesh(void* instanceId, const std::string& meshPath, math::AABB& outBounds) {
        auto* controller = getMeshController(instanceId);
        return controller && controller->loadMesh(meshPath, outBounds);
    }

    void PreviewAdapter::unloadPreviewMesh(void* instanceId) {
        auto* controller = getMeshController(instanceId);
        if (controller) {
            controller->unloadMesh();
        }
    }

    bool PreviewAdapter::isPreviewMeshLoaded(void* instanceId) const {
        auto* controller = getMeshControllerConst(instanceId);
        return controller && controller->isMeshLoaded();
    }

    std::vector<services::SubMeshInfo> PreviewAdapter::getPreviewMeshSubMeshInfo(void* instanceId) const {
        auto* controller = getMeshControllerConst(instanceId);
        if (!controller) return {};
        return controller->getSubMeshInfo();
    }

    math::AABB PreviewAdapter::getPreviewMeshBounds(void* instanceId) const {
        auto* controller = getMeshControllerConst(instanceId);
        if (!controller) return math::AABB{};
        return controller->getMeshBounds();
    }

    void PreviewAdapter::setMeshPreviewParams(void* instanceId, const services::MeshPreviewParams& params) {
        auto* controller = getMeshController(instanceId);
        if (!controller) return;
        controller->setModelMatrix(params.modelMatrix);
        controller->setHighlightedSubMesh(params.highlightedSubMesh);
    }

    void PreviewAdapter::updateMeshCamera(void* instanceId, const glm::mat4& view, const glm::mat4& projection,
                                           const glm::vec3& cameraPos) {
        auto* controller = getMeshController(instanceId);
        if (controller) {
            controller->updateCamera(view, projection, cameraPos);
        }
    }

    void* PreviewAdapter::renderMeshPreview(void* instanceId) {
        auto* controller = getMeshController(instanceId);
        return controller ? controller->render() : nullptr;
    }

}

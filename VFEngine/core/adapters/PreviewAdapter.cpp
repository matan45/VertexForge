#include "PreviewAdapter.hpp"
#include "../../graphics/controllers/MaterialPreviewController.hpp"
#include "../../graphics/controllers/MeshPreviewController.hpp"
#include "print/Logger.hpp"

namespace core {

    PreviewAdapter::PreviewAdapter() = default;

    PreviewAdapter::~PreviewAdapter() noexcept {
        
        try {
            materialControllers.clear();
        } catch (const std::exception& e) {
            loggerError("Exception during material controller cleanup: {}", e.what());
        } catch (...) {
            loggerError("Unknown exception during material controller cleanup");
        }

        try {
            meshControllers.clear();
        } catch (const std::exception& e) {
            loggerError("Exception during mesh controller cleanup: {}", e.what());
        } catch (...) {
            loggerError("Unknown exception during mesh controller cleanup");
        }
    }
    
    controllers::MaterialPreviewController* PreviewAdapter::getMaterialController(services::PreviewInstanceId instanceId) {
        return getMaterialControllerConst(instanceId);
    }

    controllers::MaterialPreviewController* PreviewAdapter::getMaterialControllerConst(services::PreviewInstanceId instanceId) const {
        auto it = materialControllers.find(instanceId);
        if (it != materialControllers.end()) {
            return it->second.get();
        }
        return nullptr;
    }

    controllers::MeshPreviewController* PreviewAdapter::getMeshController(services::PreviewInstanceId instanceId) {
        return getMeshControllerConst(instanceId);
    }

    controllers::MeshPreviewController* PreviewAdapter::getMeshControllerConst(services::PreviewInstanceId instanceId) const {
        auto it = meshControllers.find(instanceId);
        if (it != meshControllers.end()) {
            return it->second.get();
        }
        return nullptr;
    }

    // === Material Preview ===

    void PreviewAdapter::initMaterialPreview(services::PreviewInstanceId instanceId) {
        // Create a new controller for this instance if it doesn't exist
        auto& controller = materialControllers[instanceId];
        if (!controller) {
            controller = std::make_unique<::controllers::MaterialPreviewController>();
        }
        controller->init();
    }

    void PreviewAdapter::cleanUpMaterialPreview(services::PreviewInstanceId instanceId) {
        auto it = materialControllers.find(instanceId);
        if (it != materialControllers.end()) {
            if (it->second) {
                it->second->cleanUp();
            }
            materialControllers.erase(it);
        }
    }

    bool PreviewAdapter::isMaterialPreviewInitialized(services::PreviewInstanceId instanceId) const {
        auto* controller = getMaterialControllerConst(instanceId);
        return controller && controller->isInitialized();
    }

    void PreviewAdapter::setMaterialParams(services::PreviewInstanceId instanceId, const services::MaterialPreviewParams& params) {
        auto* controller = getMaterialController(instanceId);
        if (!controller) return;

        // Convert service DTO to controller params
        controllers::PreviewMaterialParams controllerParams;
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

        // Handle material data if provided for dynamic shader evaluation.
        if (params.materialDataHandle.has_value()) {
            try {
                controllerParams.materialData =
                    std::any_cast<std::shared_ptr<material::MaterialData>>(params.materialDataHandle);
            } catch (const std::bad_any_cast& e) {
                loggerError("Invalid materialDataHandle type: {}", e.what());
            }
        }

        controller->setMaterialParams(controllerParams);
    }

    services::MaterialPreviewParams PreviewAdapter::getMaterialParams(services::PreviewInstanceId instanceId) const {
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
        return result;
    }

    void PreviewAdapter::updateMaterialCamera(services::PreviewInstanceId instanceId, const glm::mat4& view, const glm::mat4& projection,
                                               const glm::vec3& cameraPos, float time) {
        auto* controller = getMaterialController(instanceId);
        if (controller) {
            controller->updateCamera(view, projection, cameraPos, time);
        }
    }

    void* PreviewAdapter::renderMaterialPreview(services::PreviewInstanceId instanceId) {
        auto* controller = getMaterialController(instanceId);
        return controller ? controller->render() : nullptr;
    }

    std::string PreviewAdapter::getMaterialShaderError(services::PreviewInstanceId instanceId) const {
        auto* controller = getMaterialControllerConst(instanceId);
        return controller ? controller->getLastShaderCompilationError() : "";
    }
    
    void PreviewAdapter::initMeshPreview(services::PreviewInstanceId instanceId) {
        // Create a new controller for this instance if it doesn't exist
        auto& controller = meshControllers[instanceId];
        if (!controller) {
            controller = std::make_unique<::controllers::MeshPreviewController>();
        }
        controller->init();
    }

    void PreviewAdapter::cleanUpMeshPreview(services::PreviewInstanceId instanceId) {
        auto it = meshControllers.find(instanceId);
        if (it != meshControllers.end()) {
            if (it->second) {
                it->second->cleanUp();
            }
            meshControllers.erase(it);
        }
    }

    bool PreviewAdapter::isMeshPreviewInitialized(services::PreviewInstanceId instanceId) const {
        auto* controller = getMeshControllerConst(instanceId);
        // MeshPreviewController doesn't have isInitialized, check if controller exists
        return controller != nullptr;
    }

    bool PreviewAdapter::loadPreviewMesh(services::PreviewInstanceId instanceId, const std::string& meshPath, math::AABB& outBounds) {
        auto* controller = getMeshController(instanceId);
        return controller && controller->loadMesh(meshPath, outBounds);
    }

    void PreviewAdapter::unloadPreviewMesh(services::PreviewInstanceId instanceId) {
        auto* controller = getMeshController(instanceId);
        if (controller) {
            controller->unloadMesh();
        }
    }

    bool PreviewAdapter::isPreviewMeshLoaded(services::PreviewInstanceId instanceId) const {
        auto* controller = getMeshControllerConst(instanceId);
        return controller && controller->isMeshLoaded();
    }

    std::vector<services::SubMeshInfo> PreviewAdapter::getPreviewMeshSubMeshInfo(services::PreviewInstanceId instanceId) const {
        auto* controller = getMeshControllerConst(instanceId);
        if (!controller) return {};
        return controller->getSubMeshInfo();
    }

    math::AABB PreviewAdapter::getPreviewMeshBounds(services::PreviewInstanceId instanceId) const {
        auto* controller = getMeshControllerConst(instanceId);
        if (!controller) return math::AABB{};
        return controller->getMeshBounds();
    }

    void PreviewAdapter::setMeshPreviewParams(services::PreviewInstanceId instanceId, const services::MeshPreviewParams& params) {
        auto* controller = getMeshController(instanceId);
        if (!controller) return;
        controller->setModelMatrix(params.modelMatrix);
        controller->setHighlightedSubMesh(params.highlightedSubMesh);
    }

    void PreviewAdapter::updateMeshCamera(services::PreviewInstanceId instanceId, const glm::mat4& view, const glm::mat4& projection,
                                           const glm::vec3& cameraPos) {
        auto* controller = getMeshController(instanceId);
        if (controller) {
            controller->updateCamera(view, projection, cameraPos);
        }
    }

    void* PreviewAdapter::renderMeshPreview(services::PreviewInstanceId instanceId) {
        auto* controller = getMeshController(instanceId);
        return controller ? controller->render() : nullptr;
    }

}

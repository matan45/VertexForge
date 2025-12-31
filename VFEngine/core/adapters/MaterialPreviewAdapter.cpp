#include "MaterialPreviewAdapter.hpp"
#include "print/Logger.hpp"

namespace core {

    MaterialPreviewAdapter::~MaterialPreviewAdapter() noexcept {
        controllers.clear();
    }

    controllers::MaterialPreviewController* MaterialPreviewAdapter::getController(services::PreviewInstanceId instanceId) const {
        auto it = controllers.find(instanceId);
        return (it != controllers.end()) ? it->second.get() : nullptr;
    }

    void MaterialPreviewAdapter::initMaterialPreview(services::PreviewInstanceId instanceId) {
        auto& controller = controllers[instanceId];
        if (!controller) {
            controller = std::make_unique<::controllers::MaterialPreviewController>();
        }
        controller->init();
    }

    void MaterialPreviewAdapter::cleanUpMaterialPreview(services::PreviewInstanceId instanceId) {
        auto it = controllers.find(instanceId);
        if (it != controllers.end()) {
            if (it->second) {
                it->second->cleanUp();
            }
            controllers.erase(it);
        }
    }

    bool MaterialPreviewAdapter::isMaterialPreviewInitialized(services::PreviewInstanceId instanceId) const {
        auto* controller = getController(instanceId);
        return controller && controller->isInitialized();
    }

    void MaterialPreviewAdapter::setMaterialParams(services::PreviewInstanceId instanceId, const services::MaterialPreviewParams& params) {
        auto* controller = getController(instanceId);
        if (!controller) return;

        controllers::PreviewMaterialParams controllerParams;
        controllerParams.albedo = params.albedo;
        controllerParams.metallic = params.metallic;
        controllerParams.roughness = params.roughness;
        controllerParams.ao = params.ao;
        controllerParams.emission = params.emission;
        controllerParams.albedoTexturePath = params.albedoTexturePath;
        controllerParams.normalTexturePath = params.normalTexturePath;
        controllerParams.ormTexturePath = params.ormTexturePath;
        controllerParams.metallicTexturePath = params.metallicTexturePath;
        controllerParams.roughnessTexturePath = params.roughnessTexturePath;
        controllerParams.aoTexturePath = params.aoTexturePath;
        controllerParams.emissionTexturePath = params.emissionTexturePath;
        controllerParams.heightTexturePath = params.heightTexturePath;
        controllerParams.materialPath = params.materialPath;
        controllerParams.useCustomShader = params.useCustomShader;

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

    services::MaterialPreviewParams MaterialPreviewAdapter::getMaterialParams(services::PreviewInstanceId instanceId) const {
        services::MaterialPreviewParams result;
        auto* controller = getController(instanceId);
        if (!controller) return result;

        const auto& controllerParams = controller->getMaterialParams();
        result.albedo = controllerParams.albedo;
        result.metallic = controllerParams.metallic;
        result.roughness = controllerParams.roughness;
        result.ao = controllerParams.ao;
        result.emission = controllerParams.emission;
        result.albedoTexturePath = controllerParams.albedoTexturePath;
        result.normalTexturePath = controllerParams.normalTexturePath;
        result.ormTexturePath = controllerParams.ormTexturePath;
        result.metallicTexturePath = controllerParams.metallicTexturePath;
        result.roughnessTexturePath = controllerParams.roughnessTexturePath;
        result.aoTexturePath = controllerParams.aoTexturePath;
        result.emissionTexturePath = controllerParams.emissionTexturePath;
        result.heightTexturePath = controllerParams.heightTexturePath;
        result.materialPath = controllerParams.materialPath;
        result.useCustomShader = controllerParams.useCustomShader;
        return result;
    }

    void MaterialPreviewAdapter::updateMaterialCamera(services::PreviewInstanceId instanceId, const glm::mat4& view, const glm::mat4& projection,
                                                       const glm::vec3& cameraPos, float time) {
        auto* controller = getController(instanceId);
        if (controller) {
            controller->updateCamera(view, projection, cameraPos, time);
        }
    }

    void* MaterialPreviewAdapter::renderMaterialPreview(services::PreviewInstanceId instanceId) {
        auto* controller = getController(instanceId);
        return controller ? controller->render() : nullptr;
    }

    std::string MaterialPreviewAdapter::getMaterialShaderError(services::PreviewInstanceId instanceId) const {
        auto* controller = getController(instanceId);
        return controller ? controller->getLastShaderCompilationError() : "";
    }

    void MaterialPreviewAdapter::initMaterialPreviewAsync(services::PreviewInstanceId instanceId) {
        auto& controller = controllers[instanceId];
        if (!controller) {
            controller = std::make_unique<::controllers::MaterialPreviewController>();
        }
        controller->initAsync();
    }

    services::IBLLoadingProgress MaterialPreviewAdapter::getIBLLoadingProgress(services::PreviewInstanceId instanceId) const {
        auto* controller = getController(instanceId);
        if (!controller) {
            services::IBLLoadingProgress progress;
            progress.state = services::LoadingState::Idle;
            return progress;
        }
        return controller->getIBLLoadingProgress();
    }

    void MaterialPreviewAdapter::cancelIBLLoading(services::PreviewInstanceId instanceId) {
        auto* controller = getController(instanceId);
        if (controller) {
            controller->cancelIBLLoading();
        }
    }

    void MaterialPreviewAdapter::processAsyncLoading() {
        for (auto& [instanceId, controller] : controllers) {
            if (controller) {
                controller->updateAsyncLoading();
            }
        }
    }

}

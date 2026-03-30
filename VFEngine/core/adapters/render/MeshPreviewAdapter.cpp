#include "MeshPreviewAdapter.hpp"

namespace core
{
    MeshPreviewAdapter::~MeshPreviewAdapter() noexcept
    {
        controllers.clear();
    }

    controllers::MeshPreviewController* MeshPreviewAdapter::getController(services::PreviewInstanceId instanceId) const
    {
        auto it = controllers.find(instanceId);
        return (it != controllers.end()) ? it->second.get() : nullptr;
    }

    void MeshPreviewAdapter::initMeshPreview(services::PreviewInstanceId instanceId)
    {
        auto& controller = controllers[instanceId];
        if (!controller)
        {
            controller = std::make_unique<::controllers::MeshPreviewController>();
        }
        controller->init();
    }

    void MeshPreviewAdapter::cleanUpMeshPreview(services::PreviewInstanceId instanceId)
    {
        auto it = controllers.find(instanceId);
        if (it != controllers.end())
        {
            if (it->second)
            {
                it->second->cleanUp();
            }
            controllers.erase(it);
        }
    }

    bool MeshPreviewAdapter::isMeshPreviewInitialized(services::PreviewInstanceId instanceId) const
    {
        auto* controller = getController(instanceId);
        return controller != nullptr;
    }

    bool MeshPreviewAdapter::isPreviewMeshLoaded(services::PreviewInstanceId instanceId) const
    {
        auto* controller = getController(instanceId);
        return controller && controller->isMeshLoaded();
    }

    std::vector<services::SubMeshInfo> MeshPreviewAdapter::getPreviewMeshSubMeshInfo(
        services::PreviewInstanceId instanceId) const
    {
        auto* controller = getController(instanceId);
        if (!controller) return {};
        return controller->getSubMeshInfo();
    }

    std::vector<services::LODInfo> MeshPreviewAdapter::getPreviewMeshLODInfo(
        services::PreviewInstanceId instanceId) const
    {
        auto* controller = getController(instanceId);
        if (!controller) return {};
        return controller->getLODInfo();
    }

    math::AABB MeshPreviewAdapter::getPreviewMeshBounds(services::PreviewInstanceId instanceId) const
    {
        auto* controller = getController(instanceId);
        if (!controller) return math::AABB{};
        return controller->getMeshBounds();
    }

    void MeshPreviewAdapter::setMeshPreviewParams(services::PreviewInstanceId instanceId,
                                                  const services::MeshPreviewParams& params)
    {
        auto* controller = getController(instanceId);
        if (!controller) return;
        controller->setModelMatrix(params.modelMatrix);
        controller->setHighlightedSubMesh(params.highlightedSubMesh);
        controller->setForceLODLevel(params.forceLODLevel);
        controller->setWireframeMode(params.wireframeMode);
        controller->setShowBoundingBox(params.showBoundingBox);
        controller->setMaterialOverrideMode(params.materialOverrideMode);
    }

    void MeshPreviewAdapter::setPreviewEnvironment(services::PreviewInstanceId instanceId,
                                                  const services::PreviewEnvironmentParams& params)
    {
        auto* controller = getController(instanceId);
        if (controller)
        {
            controller->setEnvironment(params);
        }
    }

    void MeshPreviewAdapter::updateMeshCamera(services::PreviewInstanceId instanceId, const glm::mat4& view,
                                              const glm::mat4& projection,
                                              const glm::vec3& cameraPos)
    {
        auto* controller = getController(instanceId);
        if (controller)
        {
            controller->updateCamera(view, projection, cameraPos);
        }
    }

    void* MeshPreviewAdapter::renderMeshPreview(services::PreviewInstanceId instanceId)
    {
        auto* controller = getController(instanceId);
        return controller ? controller->render() : nullptr;
    }

    void MeshPreviewAdapter::loadPreviewMeshAsync(services::PreviewInstanceId instanceId, const std::string& meshPath)
    {
        auto* controller = getController(instanceId);
        if (controller)
        {
            controller->loadMeshAsync(meshPath);
        }
    }

    void MeshPreviewAdapter::cancelMeshLoading(services::PreviewInstanceId instanceId)
    {
        auto* controller = getController(instanceId);
        if (controller)
        {
            controller->cancelMeshLoading();
        }
    }

    services::MeshLoadingProgress MeshPreviewAdapter::getMeshLoadingProgress(
        services::PreviewInstanceId instanceId) const
    {
        auto* controller = getController(instanceId);
        if (!controller)
        {
            return services::MeshLoadingProgress{};
        }
        return controller->getMeshLoadingProgress();
    }

    void MeshPreviewAdapter::processAsyncLoading()
    {
        for (auto& [instanceId, controller] : controllers)
        {
            if (controller)
            {
                controller->updateAsyncLoading();
            }
        }
    }
}

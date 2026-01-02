#include "PreviewServiceImpl.hpp"
#include "../providers/IMaterialPreviewProvider.hpp"
#include "../providers/IMeshPreviewProvider.hpp"
#include "../events/EventDispatcher.hpp"
#include <cassert>

namespace services {

    PreviewServiceImpl::PreviewServiceImpl(IMaterialPreviewProvider* materialProv, IMeshPreviewProvider* meshProv)
        : materialProvider(materialProv), meshProvider(meshProv) {
        assert(materialProvider != nullptr && "PreviewServiceImpl requires a valid IMaterialPreviewProvider");
        assert(meshProvider != nullptr && "PreviewServiceImpl requires a valid IMeshPreviewProvider");
    }

    PreviewServiceImpl::~PreviewServiceImpl() = default;

    void PreviewServiceImpl::registerEventHandlers() {
        auto& dispatcher = ::events::EventDispatcher::instance();

        // Material Preview Commands
        dispatcher.registerCommandHandler<events::preview::InitMaterialPreviewCommand>(
            [this](const events::preview::InitMaterialPreviewCommand& cmd) {
                initMaterialPreview(cmd.instanceId);
            });

        dispatcher.registerCommandHandler<events::preview::CleanUpMaterialPreviewCommand>(
            [this](const events::preview::CleanUpMaterialPreviewCommand& cmd) {
                cleanUpMaterialPreview(cmd.instanceId);
            });

        dispatcher.registerCommandHandler<events::preview::SetMaterialParamsCommand>(
            [this](const events::preview::SetMaterialParamsCommand& cmd) {
                setMaterialParams(cmd.instanceId, cmd.params);
            });

        dispatcher.registerCommandHandler<events::preview::UpdateMaterialCameraCommand>(
            [this](const events::preview::UpdateMaterialCameraCommand& cmd) {
                updateMaterialCamera(cmd.instanceId, cmd.view, cmd.projection, cmd.cameraPos, cmd.time);
            });

        // Material Preview Queries
        dispatcher.registerQueryHandler<events::preview::RenderMaterialPreviewQuery>(
            [this](const events::preview::RenderMaterialPreviewQuery& query) {
                return renderMaterialPreview(query.instanceId);
            });

        dispatcher.registerQueryHandler<events::preview::GetMaterialShaderErrorQuery>(
            [this](const events::preview::GetMaterialShaderErrorQuery& query) {
                return getMaterialShaderError(query.instanceId);
            });

        // Mesh Preview Commands
        dispatcher.registerCommandHandler<events::preview::InitMeshPreviewCommand>(
            [this](const events::preview::InitMeshPreviewCommand& cmd) {
                initMeshPreview(cmd.instanceId);
            });

        dispatcher.registerCommandHandler<events::preview::CleanUpMeshPreviewCommand>(
            [this](const events::preview::CleanUpMeshPreviewCommand& cmd) {
                cleanUpMeshPreview(cmd.instanceId);
            });

        dispatcher.registerCommandHandler<events::preview::SetMeshPreviewParamsCommand>(
            [this](const events::preview::SetMeshPreviewParamsCommand& cmd) {
                setMeshPreviewParams(cmd.instanceId, cmd.params);
            });

        dispatcher.registerCommandHandler<events::preview::UpdateMeshCameraCommand>(
            [this](const events::preview::UpdateMeshCameraCommand& cmd) {
                updateMeshCamera(cmd.instanceId, cmd.view, cmd.projection, cmd.cameraPos);
            });

        // Mesh Preview Queries
        dispatcher.registerQueryHandler<events::preview::GetPreviewMeshSubMeshInfoQuery>(
            [this](const events::preview::GetPreviewMeshSubMeshInfoQuery& query) {
                return getPreviewMeshSubMeshInfo(query.instanceId);
            });

        dispatcher.registerQueryHandler<events::preview::GetPreviewMeshLODInfoQuery>(
            [this](const events::preview::GetPreviewMeshLODInfoQuery& query) {
                return getPreviewMeshLODInfo(query.instanceId);
            });

        dispatcher.registerQueryHandler<events::preview::GetPreviewMeshBoundsQuery>(
            [this](const events::preview::GetPreviewMeshBoundsQuery& query) {
                return getPreviewMeshBounds(query.instanceId);
            });

        dispatcher.registerQueryHandler<events::preview::RenderMeshPreviewQuery>(
            [this](const events::preview::RenderMeshPreviewQuery& query) {
                return renderMeshPreview(query.instanceId);
            });

        // Async Mesh Loading Commands
        dispatcher.registerCommandHandler<events::preview::LoadPreviewMeshAsyncCommand>(
            [this](const events::preview::LoadPreviewMeshAsyncCommand& cmd) {
                loadPreviewMeshAsync(cmd.instanceId, cmd.meshPath);
            });

        dispatcher.registerCommandHandler<events::preview::CancelMeshLoadingCommand>(
            [this](const events::preview::CancelMeshLoadingCommand& cmd) {
                cancelMeshLoading(cmd.instanceId);
            });

        // Async Mesh Loading Queries
        dispatcher.registerQueryHandler<events::preview::GetMeshLoadingProgressQuery>(
            [this](const events::preview::GetMeshLoadingProgressQuery& query) {
                return getMeshLoadingProgress(query.instanceId);
            });
    }

    // === Material Preview ===

    void PreviewServiceImpl::initMaterialPreview(PreviewInstanceId instanceId) {
        materialProvider->initMaterialPreview(instanceId);
    }

    void PreviewServiceImpl::cleanUpMaterialPreview(PreviewInstanceId instanceId) {
        materialProvider->cleanUpMaterialPreview(instanceId);
    }

    void PreviewServiceImpl::setMaterialParams(PreviewInstanceId instanceId, const MaterialPreviewParams& params) {
        materialProvider->setMaterialParams(instanceId, params);
    }

    void PreviewServiceImpl::updateMaterialCamera(PreviewInstanceId instanceId, const glm::mat4& view, const glm::mat4& projection,
                                                   const glm::vec3& cameraPos, float time) {
        materialProvider->updateMaterialCamera(instanceId, view, projection, cameraPos, time);
    }

    ViewportTextureHandle PreviewServiceImpl::renderMaterialPreview(PreviewInstanceId instanceId) {
        ViewportTextureHandle handle;
        handle.imguiDescriptorSet = materialProvider->renderMaterialPreview(instanceId);
        return handle;
    }

    std::string PreviewServiceImpl::getMaterialShaderError(PreviewInstanceId instanceId) const {
        return materialProvider->getMaterialShaderError(instanceId);
    }

    // === Mesh Preview ===

    void PreviewServiceImpl::initMeshPreview(PreviewInstanceId instanceId) {
        meshProvider->initMeshPreview(instanceId);
    }

    void PreviewServiceImpl::cleanUpMeshPreview(PreviewInstanceId instanceId) {
        meshProvider->cleanUpMeshPreview(instanceId);
    }

    std::vector<SubMeshInfo> PreviewServiceImpl::getPreviewMeshSubMeshInfo(PreviewInstanceId instanceId) const {
        return meshProvider->getPreviewMeshSubMeshInfo(instanceId);
    }

    std::vector<LODInfo> PreviewServiceImpl::getPreviewMeshLODInfo(PreviewInstanceId instanceId) const {
        return meshProvider->getPreviewMeshLODInfo(instanceId);
    }

    math::AABB PreviewServiceImpl::getPreviewMeshBounds(PreviewInstanceId instanceId) const {
        return meshProvider->getPreviewMeshBounds(instanceId);
    }

    void PreviewServiceImpl::setMeshPreviewParams(PreviewInstanceId instanceId, const MeshPreviewParams& params) {
        meshProvider->setMeshPreviewParams(instanceId, params);
    }

    void PreviewServiceImpl::updateMeshCamera(PreviewInstanceId instanceId, const glm::mat4& view, const glm::mat4& projection,
                                               const glm::vec3& cameraPos) {
        meshProvider->updateMeshCamera(instanceId, view, projection, cameraPos);
    }

    ViewportTextureHandle PreviewServiceImpl::renderMeshPreview(PreviewInstanceId instanceId) {
        ViewportTextureHandle handle;
        handle.imguiDescriptorSet = meshProvider->renderMeshPreview(instanceId);
        return handle;
    }

    // === Async Mesh Loading ===

    void PreviewServiceImpl::loadPreviewMeshAsync(PreviewInstanceId instanceId, const std::string& meshPath) {
        meshProvider->loadPreviewMeshAsync(instanceId, meshPath);
    }

    void PreviewServiceImpl::cancelMeshLoading(PreviewInstanceId instanceId) {
        meshProvider->cancelMeshLoading(instanceId);
    }

    MeshLoadingProgress PreviewServiceImpl::getMeshLoadingProgress(PreviewInstanceId instanceId) const {
        return meshProvider->getMeshLoadingProgress(instanceId);
    }

    void PreviewServiceImpl::processAsyncLoading() {
        meshProvider->processAsyncLoading();
    }

}

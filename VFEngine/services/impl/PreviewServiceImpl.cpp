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
        dispatcher.registerQueryHandler<events::preview::IsMaterialPreviewReadyQuery>(
            [this](const events::preview::IsMaterialPreviewReadyQuery& query) {
                return isMaterialPreviewReady(query.instanceId);
            });

        dispatcher.registerQueryHandler<events::preview::GetMaterialParamsQuery>(
            [this](const events::preview::GetMaterialParamsQuery& query) {
                return getMaterialParams(query.instanceId);
            });

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

        dispatcher.registerCommandHandler<events::preview::LoadPreviewMeshCommand>(
            [this](const events::preview::LoadPreviewMeshCommand& cmd) {
                math::AABB bounds;
                bool result = loadPreviewMesh(cmd.instanceId, cmd.meshPath, bounds);
                return events::preview::LoadPreviewMeshResult{ result, bounds };
            });

        dispatcher.registerCommandHandler<events::preview::UnloadPreviewMeshCommand>(
            [this](const events::preview::UnloadPreviewMeshCommand& cmd) {
                unloadPreviewMesh(cmd.instanceId);
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
        dispatcher.registerQueryHandler<events::preview::IsMeshPreviewReadyQuery>(
            [this](const events::preview::IsMeshPreviewReadyQuery& query) {
                return isMeshPreviewReady(query.instanceId);
            });

        dispatcher.registerQueryHandler<events::preview::IsPreviewMeshLoadedQuery>(
            [this](const events::preview::IsPreviewMeshLoadedQuery& query) {
                return isPreviewMeshLoaded(query.instanceId);
            });

        dispatcher.registerQueryHandler<events::preview::GetPreviewMeshSubMeshInfoQuery>(
            [this](const events::preview::GetPreviewMeshSubMeshInfoQuery& query) {
                return getPreviewMeshSubMeshInfo(query.instanceId);
            });

        dispatcher.registerQueryHandler<events::preview::GetPreviewMeshBoundsQuery>(
            [this](const events::preview::GetPreviewMeshBoundsQuery& query) {
                return getPreviewMeshBounds(query.instanceId);
            });

        dispatcher.registerQueryHandler<events::preview::RenderMeshPreviewQuery>(
            [this](const events::preview::RenderMeshPreviewQuery& query) {
                return renderMeshPreview(query.instanceId);
            });
    }

    // === Material Preview ===

    void PreviewServiceImpl::initMaterialPreview(PreviewInstanceId instanceId) {
        materialProvider->initMaterialPreview(instanceId);
    }

    void PreviewServiceImpl::cleanUpMaterialPreview(PreviewInstanceId instanceId) {
        materialProvider->cleanUpMaterialPreview(instanceId);
    }

    bool PreviewServiceImpl::isMaterialPreviewReady(PreviewInstanceId instanceId) const {
        return materialProvider->isMaterialPreviewInitialized(instanceId);
    }

    void PreviewServiceImpl::setMaterialParams(PreviewInstanceId instanceId, const MaterialPreviewParams& params) {
        materialProvider->setMaterialParams(instanceId, params);
    }

    MaterialPreviewParams PreviewServiceImpl::getMaterialParams(PreviewInstanceId instanceId) const {
        return materialProvider->getMaterialParams(instanceId);
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

    bool PreviewServiceImpl::isMeshPreviewReady(PreviewInstanceId instanceId) const {
        return meshProvider->isMeshPreviewInitialized(instanceId);
    }

    bool PreviewServiceImpl::loadPreviewMesh(PreviewInstanceId instanceId, const std::string& meshPath, math::AABB& outBounds) {
        return meshProvider->loadPreviewMesh(instanceId, meshPath, outBounds);
    }

    void PreviewServiceImpl::unloadPreviewMesh(PreviewInstanceId instanceId) {
        meshProvider->unloadPreviewMesh(instanceId);
    }

    bool PreviewServiceImpl::isPreviewMeshLoaded(PreviewInstanceId instanceId) const {
        return meshProvider->isPreviewMeshLoaded(instanceId);
    }

    std::vector<SubMeshInfo> PreviewServiceImpl::getPreviewMeshSubMeshInfo(PreviewInstanceId instanceId) const {
        return meshProvider->getPreviewMeshSubMeshInfo(instanceId);
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

}

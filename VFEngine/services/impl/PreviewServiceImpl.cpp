#include "PreviewServiceImpl.hpp"
#include "../providers/IPreviewProvider.hpp"
#include "../events/EventDispatcher.hpp"

namespace services {

    PreviewServiceImpl::PreviewServiceImpl(IPreviewProvider* provider)
        : provider(provider) {}

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

    void PreviewServiceImpl::initMaterialPreview(void* instanceId) {
        if (provider) {
            provider->initMaterialPreview(instanceId);
        }
    }

    void PreviewServiceImpl::cleanUpMaterialPreview(void* instanceId) {
        if (provider) {
            provider->cleanUpMaterialPreview(instanceId);
        }
    }

    bool PreviewServiceImpl::isMaterialPreviewReady(void* instanceId) const {
        return provider && provider->isMaterialPreviewInitialized(instanceId);
    }

    void PreviewServiceImpl::setMaterialParams(void* instanceId, const MaterialPreviewParams& params) {
        if (provider) {
            provider->setMaterialParams(instanceId, params);
        }
    }

    MaterialPreviewParams PreviewServiceImpl::getMaterialParams(void* instanceId) const {
        return provider ? provider->getMaterialParams(instanceId) : MaterialPreviewParams{};
    }

    void PreviewServiceImpl::updateMaterialCamera(void* instanceId, const glm::mat4& view, const glm::mat4& projection,
                                                   const glm::vec3& cameraPos, float time) {
        if (provider) {
            provider->updateMaterialCamera(instanceId, view, projection, cameraPos, time);
        }
    }

    ViewportTextureHandle PreviewServiceImpl::renderMaterialPreview(void* instanceId) {
        if (!provider) {
            return ViewportTextureHandle{};
        }

        ViewportTextureHandle handle;
        handle.imguiDescriptorSet = provider->renderMaterialPreview(instanceId);
        return handle;
    }

    std::string PreviewServiceImpl::getMaterialShaderError(void* instanceId) const {
        return provider ? provider->getMaterialShaderError(instanceId) : "";
    }

    // === Mesh Preview ===

    void PreviewServiceImpl::initMeshPreview(void* instanceId) {
        if (provider) {
            provider->initMeshPreview(instanceId);
        }
    }

    void PreviewServiceImpl::cleanUpMeshPreview(void* instanceId) {
        if (provider) {
            provider->cleanUpMeshPreview(instanceId);
        }
    }

    bool PreviewServiceImpl::isMeshPreviewReady(void* instanceId) const {
        return provider && provider->isMeshPreviewInitialized(instanceId);
    }

    bool PreviewServiceImpl::loadPreviewMesh(void* instanceId, const std::string& meshPath, math::AABB& outBounds) {
        return provider && provider->loadPreviewMesh(instanceId, meshPath, outBounds);
    }

    void PreviewServiceImpl::unloadPreviewMesh(void* instanceId) {
        if (provider) {
            provider->unloadPreviewMesh(instanceId);
        }
    }

    bool PreviewServiceImpl::isPreviewMeshLoaded(void* instanceId) const {
        return provider && provider->isPreviewMeshLoaded(instanceId);
    }

    std::vector<SubMeshInfo> PreviewServiceImpl::getPreviewMeshSubMeshInfo(void* instanceId) const {
        return provider ? provider->getPreviewMeshSubMeshInfo(instanceId) : std::vector<SubMeshInfo>{};
    }

    math::AABB PreviewServiceImpl::getPreviewMeshBounds(void* instanceId) const {
        return provider ? provider->getPreviewMeshBounds(instanceId) : math::AABB{};
    }

    void PreviewServiceImpl::setMeshPreviewParams(void* instanceId, const MeshPreviewParams& params) {
        if (provider) {
            provider->setMeshPreviewParams(instanceId, params);
        }
    }

    void PreviewServiceImpl::updateMeshCamera(void* instanceId, const glm::mat4& view, const glm::mat4& projection,
                                               const glm::vec3& cameraPos) {
        if (provider) {
            provider->updateMeshCamera(instanceId, view, projection, cameraPos);
        }
    }

    ViewportTextureHandle PreviewServiceImpl::renderMeshPreview(void* instanceId) {
        if (!provider) {
            return ViewportTextureHandle{};
        }

        ViewportTextureHandle handle;
        handle.imguiDescriptorSet = provider->renderMeshPreview(instanceId);
        return handle;
    }

}

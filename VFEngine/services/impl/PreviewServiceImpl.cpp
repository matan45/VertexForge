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
            [this](const events::preview::InitMaterialPreviewCommand&) {
                initMaterialPreview();
            });

        dispatcher.registerCommandHandler<events::preview::CleanUpMaterialPreviewCommand>(
            [this](const events::preview::CleanUpMaterialPreviewCommand&) {
                cleanUpMaterialPreview();
            });

        dispatcher.registerCommandHandler<events::preview::SetMaterialParamsCommand>(
            [this](const events::preview::SetMaterialParamsCommand& cmd) {
                setMaterialParams(cmd.params);
            });

        dispatcher.registerCommandHandler<events::preview::UpdateMaterialCameraCommand>(
            [this](const events::preview::UpdateMaterialCameraCommand& cmd) {
                updateMaterialCamera(cmd.view, cmd.projection, cmd.cameraPos, cmd.time);
            });

        // Material Preview Queries
        dispatcher.registerQueryHandler<events::preview::IsMaterialPreviewReadyQuery>(
            [this](const events::preview::IsMaterialPreviewReadyQuery&) {
                return isMaterialPreviewReady();
            });

        dispatcher.registerQueryHandler<events::preview::GetMaterialParamsQuery>(
            [this](const events::preview::GetMaterialParamsQuery&) {
                return getMaterialParams();
            });

        dispatcher.registerQueryHandler<events::preview::RenderMaterialPreviewQuery>(
            [this](const events::preview::RenderMaterialPreviewQuery&) {
                return renderMaterialPreview();
            });

        dispatcher.registerQueryHandler<events::preview::GetMaterialShaderErrorQuery>(
            [this](const events::preview::GetMaterialShaderErrorQuery&) {
                return getMaterialShaderError();
            });

        // Mesh Preview Commands
        dispatcher.registerCommandHandler<events::preview::InitMeshPreviewCommand>(
            [this](const events::preview::InitMeshPreviewCommand&) {
                initMeshPreview();
            });

        dispatcher.registerCommandHandler<events::preview::CleanUpMeshPreviewCommand>(
            [this](const events::preview::CleanUpMeshPreviewCommand&) {
                cleanUpMeshPreview();
            });

        dispatcher.registerCommandHandler<events::preview::LoadPreviewMeshCommand>(
            [this](const events::preview::LoadPreviewMeshCommand& cmd) {
                math::AABB bounds;
                bool result = loadPreviewMesh(cmd.meshPath, bounds);
                return events::preview::LoadPreviewMeshResult{ result, bounds };
            });

        dispatcher.registerCommandHandler<events::preview::UnloadPreviewMeshCommand>(
            [this](const events::preview::UnloadPreviewMeshCommand&) {
                unloadPreviewMesh();
            });

        dispatcher.registerCommandHandler<events::preview::SetMeshPreviewParamsCommand>(
            [this](const events::preview::SetMeshPreviewParamsCommand& cmd) {
                setMeshPreviewParams(cmd.params);
            });

        dispatcher.registerCommandHandler<events::preview::UpdateMeshCameraCommand>(
            [this](const events::preview::UpdateMeshCameraCommand& cmd) {
                updateMeshCamera(cmd.view, cmd.projection, cmd.cameraPos);
            });

        // Mesh Preview Queries
        dispatcher.registerQueryHandler<events::preview::IsMeshPreviewReadyQuery>(
            [this](const events::preview::IsMeshPreviewReadyQuery&) {
                return isMeshPreviewReady();
            });

        dispatcher.registerQueryHandler<events::preview::IsPreviewMeshLoadedQuery>(
            [this](const events::preview::IsPreviewMeshLoadedQuery&) {
                return isPreviewMeshLoaded();
            });

        dispatcher.registerQueryHandler<events::preview::GetPreviewMeshSubMeshInfoQuery>(
            [this](const events::preview::GetPreviewMeshSubMeshInfoQuery&) {
                return getPreviewMeshSubMeshInfo();
            });

        dispatcher.registerQueryHandler<events::preview::GetPreviewMeshBoundsQuery>(
            [this](const events::preview::GetPreviewMeshBoundsQuery&) {
                return getPreviewMeshBounds();
            });

        dispatcher.registerQueryHandler<events::preview::RenderMeshPreviewQuery>(
            [this](const events::preview::RenderMeshPreviewQuery&) {
                return renderMeshPreview();
            });
    }

    // === Material Preview ===

    void PreviewServiceImpl::initMaterialPreview() {
        if (provider) {
            provider->initMaterialPreview();
        }
    }

    void PreviewServiceImpl::cleanUpMaterialPreview() {
        if (provider) {
            provider->cleanUpMaterialPreview();
        }
    }

    bool PreviewServiceImpl::isMaterialPreviewReady() const {
        return provider && provider->isMaterialPreviewInitialized();
    }

    void PreviewServiceImpl::setMaterialParams(const MaterialPreviewParams& params) {
        if (provider) {
            provider->setMaterialParams(params);
        }
    }

    MaterialPreviewParams PreviewServiceImpl::getMaterialParams() const {
        return provider ? provider->getMaterialParams() : MaterialPreviewParams{};
    }

    void PreviewServiceImpl::updateMaterialCamera(const glm::mat4& view, const glm::mat4& projection,
                                                   const glm::vec3& cameraPos, float time) {
        if (provider) {
            provider->updateMaterialCamera(view, projection, cameraPos, time);
        }
    }

    ViewportTextureHandle PreviewServiceImpl::renderMaterialPreview() {
        if (!provider) {
            return ViewportTextureHandle{};
        }

        ViewportTextureHandle handle;
        handle.imguiDescriptorSet = provider->renderMaterialPreview();
        return handle;
    }

    std::string PreviewServiceImpl::getMaterialShaderError() const {
        return provider ? provider->getMaterialShaderError() : "";
    }

    // === Mesh Preview ===

    void PreviewServiceImpl::initMeshPreview() {
        if (provider) {
            provider->initMeshPreview();
        }
    }

    void PreviewServiceImpl::cleanUpMeshPreview() {
        if (provider) {
            provider->cleanUpMeshPreview();
        }
    }

    bool PreviewServiceImpl::isMeshPreviewReady() const {
        return provider && provider->isMeshPreviewInitialized();
    }

    bool PreviewServiceImpl::loadPreviewMesh(const std::string& meshPath, math::AABB& outBounds) {
        return provider && provider->loadPreviewMesh(meshPath, outBounds);
    }

    void PreviewServiceImpl::unloadPreviewMesh() {
        if (provider) {
            provider->unloadPreviewMesh();
        }
    }

    bool PreviewServiceImpl::isPreviewMeshLoaded() const {
        return provider && provider->isPreviewMeshLoaded();
    }

    std::vector<SubMeshInfo> PreviewServiceImpl::getPreviewMeshSubMeshInfo() const {
        return provider ? provider->getPreviewMeshSubMeshInfo() : std::vector<SubMeshInfo>{};
    }

    math::AABB PreviewServiceImpl::getPreviewMeshBounds() const {
        return provider ? provider->getPreviewMeshBounds() : math::AABB{};
    }

    void PreviewServiceImpl::setMeshPreviewParams(const MeshPreviewParams& params) {
        if (provider) {
            provider->setMeshPreviewParams(params);
        }
    }

    void PreviewServiceImpl::updateMeshCamera(const glm::mat4& view, const glm::mat4& projection,
                                               const glm::vec3& cameraPos) {
        if (provider) {
            provider->updateMeshCamera(view, projection, cameraPos);
        }
    }

    ViewportTextureHandle PreviewServiceImpl::renderMeshPreview() {
        if (!provider) {
            return ViewportTextureHandle{};
        }

        ViewportTextureHandle handle;
        handle.imguiDescriptorSet = provider->renderMeshPreview();
        return handle;
    }

}

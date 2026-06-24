#include "PreviewServiceImpl.hpp"
#include "../../providers/render/IUILayerPreviewProvider.hpp"
#include "../../events/EventDispatcher.hpp"

// VK-1435 — UI Layer Builder Preview service methods + CQRS handler registration.
// Split out of PreviewServiceImpl.cpp the way PreviewServicePrefabRig.cpp is; every method
// delegates to uiLayerProvider (which may be null in headless/test/runtime contexts, so each
// is null-guarded and registration is skipped entirely when the provider is absent).

namespace services
{
    void PreviewServiceImpl::registerUILayerPreviewHandlers(::events::EventDispatcher& dispatcher)
    {
        if (!uiLayerProvider)
        {
            return;
        }

        using namespace events::uilayerpreview;

        dispatcher.registerCommandHandler<InitUILayerPreviewCommand>(
            [this](const InitUILayerPreviewCommand& cmd)
            {
                initUILayerPreview(cmd.instanceId);
            });

        dispatcher.registerCommandHandler<BuildUILayerPreviewCommand>(
            [this](const BuildUILayerPreviewCommand& cmd) -> bool
            {
                return buildUILayerPreview(cmd.instanceId, cmd.canvasRoot, cmd.refWidth, cmd.refHeight);
            });

        dispatcher.registerCommandHandler<CleanUpUILayerPreviewCommand>(
            [this](const CleanUpUILayerPreviewCommand& cmd)
            {
                cleanUpUILayerPreview(cmd.instanceId);
            });

        dispatcher.registerCommandHandler<SetUILayerReferenceResolutionCommand>(
            [this](const SetUILayerReferenceResolutionCommand& cmd)
            {
                setUILayerReferenceResolution(cmd.instanceId, cmd.refWidth, cmd.refHeight);
            });

        dispatcher.registerQueryHandler<RenderUILayerPreviewQuery>(
            [this](const RenderUILayerPreviewQuery& query)
            {
                return renderUILayerPreview(query.instanceId);
            });

        dispatcher.registerQueryHandler<IsUILayerPreviewBuiltQuery>(
            [this](const IsUILayerPreviewBuiltQuery& query)
            {
                return isUILayerPreviewBuilt(query.instanceId);
            });

        dispatcher.registerQueryHandler<PickUILayerElementAtQuery>(
            [this](const PickUILayerElementAtQuery& query)
            {
                return pickUILayerElementAt(query.instanceId, query.refPx);
            });

        dispatcher.registerQueryHandler<GetUILayerResolvedRectQuery>(
            [this](const GetUILayerResolvedRectQuery& query)
            {
                return getUILayerResolvedRect(query.instanceId, query.entity);
            });
    }

    // ---- Delegating method bodies ----

    void PreviewServiceImpl::initUILayerPreview(PreviewInstanceId instanceId)
    {
        if (uiLayerProvider)
            uiLayerProvider->initUILayerPreview(instanceId);
    }

    bool PreviewServiceImpl::buildUILayerPreview(PreviewInstanceId instanceId, EntityHandle canvasRoot,
                                                 uint32_t refWidth, uint32_t refHeight)
    {
        return uiLayerProvider
                   ? uiLayerProvider->buildUILayerPreview(instanceId, canvasRoot, refWidth, refHeight)
                   : false;
    }

    void PreviewServiceImpl::cleanUpUILayerPreview(PreviewInstanceId instanceId)
    {
        if (uiLayerProvider)
            uiLayerProvider->cleanUpUILayerPreview(instanceId);
    }

    bool PreviewServiceImpl::isUILayerPreviewBuilt(PreviewInstanceId instanceId) const
    {
        return uiLayerProvider ? uiLayerProvider->isUILayerPreviewBuilt(instanceId) : false;
    }

    void PreviewServiceImpl::setUILayerReferenceResolution(PreviewInstanceId instanceId,
                                                           uint32_t refWidth, uint32_t refHeight)
    {
        if (uiLayerProvider)
            uiLayerProvider->setUILayerReferenceResolution(instanceId, refWidth, refHeight);
    }

    ViewportTextureHandle PreviewServiceImpl::renderUILayerPreview(PreviewInstanceId instanceId)
    {
        ViewportTextureHandle handle;
        if (uiLayerProvider)
            handle.imguiDescriptorSet = uiLayerProvider->renderUILayerPreview(instanceId);
        return handle;
    }

    EntityHandle PreviewServiceImpl::pickUILayerElementAt(PreviewInstanceId instanceId, glm::vec2 refPx) const
    {
        return uiLayerProvider ? uiLayerProvider->pickUILayerElementAt(instanceId, refPx)
                               : EntityHandle::invalid();
    }

    std::optional<UIResolvedRectData> PreviewServiceImpl::getUILayerResolvedRect(PreviewInstanceId instanceId,
                                                                                 EntityHandle entity) const
    {
        return uiLayerProvider ? uiLayerProvider->getUILayerResolvedRect(instanceId, entity) : std::nullopt;
    }
}

#include "UILayerPreviewAdapter.hpp"
#include "../../services/data/EntityConversion.hpp"

namespace core
{
    UILayerPreviewAdapter::~UILayerPreviewAdapter() noexcept
    {
        controllers.clear();
    }

    ::controllers::UILayerPreviewController*
    UILayerPreviewAdapter::getController(services::PreviewInstanceId instanceId) const
    {
        auto it = controllers.find(instanceId);
        return (it != controllers.end()) ? it->second.get() : nullptr;
    }

    void UILayerPreviewAdapter::initUILayerPreview(services::PreviewInstanceId instanceId)
    {
        auto& controller = controllers[instanceId];
        if (!controller)
        {
            controller = std::make_unique<::controllers::UILayerPreviewController>();
        }
        controller->init();
    }

    bool UILayerPreviewAdapter::buildUILayerPreview(services::PreviewInstanceId instanceId,
                                                    services::EntityHandle canvasRoot,
                                                    uint32_t refWidth, uint32_t refHeight)
    {
        auto* controller = getController(instanceId);
        if (!controller) return false;
        return controller->buildFromCanvas(services::internal::fromHandle(canvasRoot), refWidth, refHeight);
    }

    void UILayerPreviewAdapter::cleanUpUILayerPreview(services::PreviewInstanceId instanceId)
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

    bool UILayerPreviewAdapter::isUILayerPreviewBuilt(services::PreviewInstanceId instanceId) const
    {
        auto* controller = getController(instanceId);
        return controller && controller->isBuilt();
    }

    void UILayerPreviewAdapter::setUILayerReferenceResolution(services::PreviewInstanceId instanceId,
                                                              uint32_t refWidth, uint32_t refHeight)
    {
        auto* controller = getController(instanceId);
        if (controller)
        {
            controller->setReferenceResolution(refWidth, refHeight);
        }
    }

    void* UILayerPreviewAdapter::renderUILayerPreview(services::PreviewInstanceId instanceId)
    {
        auto* controller = getController(instanceId);
        return controller ? controller->render() : nullptr;
    }

    services::EntityHandle UILayerPreviewAdapter::pickUILayerElementAt(services::PreviewInstanceId instanceId,
                                                                       glm::vec2 refPx) const
    {
        auto* controller = getController(instanceId);
        if (!controller) return services::EntityHandle::invalid();

        entt::entity hit = controller->pickElementAt(refPx);
        if (hit == entt::null) return services::EntityHandle::invalid();
        return services::internal::toHandle(hit);
    }

    std::optional<services::UIResolvedRectData> UILayerPreviewAdapter::getUILayerResolvedRect(
        services::PreviewInstanceId instanceId, services::EntityHandle entity) const
    {
        auto* controller = getController(instanceId);
        if (!controller) return std::nullopt;

        std::optional<glm::vec4> rect = controller->resolvedRect(services::internal::fromHandle(entity));
        if (!rect) return std::nullopt;

        return services::UIResolvedRectData{rect->x, rect->y, rect->z, rect->w};
    }
}

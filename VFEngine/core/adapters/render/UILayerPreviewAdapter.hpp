#pragma once

// VK-1435 — Core-side adapter for the UI Layer Builder preview provider.
//
// Owns one UILayerPreviewController (Layer B) per PreviewInstanceId. Mirrors
// PrefabRigPreviewAdapter's map/getController shape, but carries NO subtree descriptor:
// the authored UI entities live in the one singleton registry, so buildUILayerPreview is
// handed only the live canvas-root EntityHandle (unwrapped to entt::entity here) plus a
// reference resolution. Element edits never come through here — they go through the
// existing UIComponentService CQRS. The Editor never sees the controller.

#include "../../services/providers/render/IUILayerPreviewProvider.hpp"
#include "../../graphics/controllers/preview/UILayerPreviewController.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace core
{
    class UILayerPreviewAdapter : public services::IUILayerPreviewProvider
    {
    private:
        std::unordered_map<services::PreviewInstanceId,
                           std::unique_ptr<::controllers::UILayerPreviewController>> controllers;
        std::vector<std::string> fontFallbackChain;

    public:
        explicit UILayerPreviewAdapter() = default;
        ~UILayerPreviewAdapter() noexcept override;

        void initUILayerPreview(services::PreviewInstanceId instanceId) override;
        bool buildUILayerPreview(services::PreviewInstanceId instanceId, services::EntityHandle canvasRoot,
                                 uint32_t refWidth, uint32_t refHeight) override;
        void cleanUpUILayerPreview(services::PreviewInstanceId instanceId) override;
        bool isUILayerPreviewBuilt(services::PreviewInstanceId instanceId) const override;

        void setUILayerReferenceResolution(services::PreviewInstanceId instanceId,
                                           uint32_t refWidth, uint32_t refHeight) override;
        void setFontFallbackChain(std::span<const std::string> fontPaths) override;

        void* renderUILayerPreview(services::PreviewInstanceId instanceId) override;

        services::EntityHandle pickUILayerElementAt(services::PreviewInstanceId instanceId,
                                                    glm::vec2 refPx) const override;

        std::optional<services::UIResolvedRectData> getUILayerResolvedRect(
            services::PreviewInstanceId instanceId, services::EntityHandle entity) const override;

    private:
        ::controllers::UILayerPreviewController* getController(services::PreviewInstanceId instanceId) const;
    };
}

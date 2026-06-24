#include "UIComponentService.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/ui/UIEvents.hpp"
#include "../../events/project/ApplicationEvents.hpp"
#include "ui/UIRectMath.hpp"

namespace services {

    // ========== UI Canvas Operations ==========

    bool UIComponentService::addUICanvasComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        if (sceneEntity.hasComponent<components::UICanvasComponent>()) {
            return false;
        }

        sceneEntity.addComponent<components::UICanvasComponent>();

        return true;
    }

    bool UIComponentService::removeUICanvasComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UICanvasComponent>()) {
            return false;
        }

        sceneEntity.removeComponent<components::UICanvasComponent>();

        // Auto-remove UIRectComponent alongside canvas
        if (sceneEntity.hasComponent<components::UIRectComponent>()) {
            sceneEntity.removeComponent<components::UIRectComponent>();
        }

        return true;
    }

    bool UIComponentService::hasUICanvasComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::UICanvasComponent>();
    }

    std::optional<UICanvasData> UIComponentService::getUICanvasData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UICanvasComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::UICanvasComponent>();

        UICanvasData data;
        data.referenceWidth = comp.referenceWidth;
        data.referenceHeight = comp.referenceHeight;
        data.scaleMode = static_cast<uint8_t>(comp.scaleMode);
        data.pixelsPerUnit = comp.pixelsPerUnit;
        data.sortOrder = comp.sortOrder;
        return data;
    }

    bool UIComponentService::setUICanvasData(EntityHandle entity, const UICanvasData& canvasData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UICanvasComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UICanvasComponent>();
        comp.referenceWidth = canvasData.referenceWidth;
        comp.referenceHeight = canvasData.referenceHeight;
        comp.scaleMode = static_cast<components::UIScaleMode>(canvasData.scaleMode);
        comp.pixelsPerUnit = canvasData.pixelsPerUnit;
        comp.sortOrder = canvasData.sortOrder;
        return true;
    }

    // VK-1435 — tag/untag the UI Layer Builder sandbox root. Tagging adds the marker AND marks
    // the root inactive: the serializer skips a tagged child without recursing, and an inactive
    // root makes scene::Entity::isEffectivelyActive false for the whole subtree so EVERY main-pass
    // UI emitter (images, labels, widget sub-draws, window chrome — most of which iterate component
    // views, not canvas roots) excludes it in both edit and play mode. The offscreen preview still
    // renders it because UILayerPreviewController's scoped path uses isEffectivelyActiveWithin,
    // which treats the sandbox root as active. Untagging restores the active flag. No descendant
    // propagation is required (isEffectivelyActive walks up to the root).
    bool UIComponentService::markUIPreviewSandbox(EntityHandle entity, bool tagged) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        entt::entity ent = internal::fromHandle(entity);
        if (tagged) {
            registry.emplace_or_replace<components::UIPreviewTagComponent>(ent);
            if (registry.all_of<components::NameComponent>(ent)) {
                registry.get<components::NameComponent>(ent).isActive = false;
            }
        } else {
            registry.remove<components::UIPreviewTagComponent>(ent);
            if (registry.all_of<components::NameComponent>(ent)) {
                registry.get<components::NameComponent>(ent).isActive = true;
            }
        }
        return true;
    }

    // ========== UI Rect Operations ==========

    bool UIComponentService::addUIRectComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        if (sceneEntity.hasComponent<components::UIRectComponent>()) {
            return false;
        }

        sceneEntity.addComponent<components::UIRectComponent>();
        return true;
    }

    bool UIComponentService::removeUIRectComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIRectComponent>()) {
            return false;
        }

        sceneEntity.removeComponent<components::UIRectComponent>();
        return true;
    }

    bool UIComponentService::hasUIRectComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::UIRectComponent>();
    }

    std::optional<UIRectData> UIComponentService::getUIRectData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIRectComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::UIRectComponent>();

        UIRectData data;
        data.anchorMin = comp.anchorMin;
        data.anchorMax = comp.anchorMax;
        data.pivot = comp.pivot;
        data.sizeDelta = comp.sizeDelta;
        data.anchoredPosition = comp.anchoredPosition;
        data.blocksRaycast = comp.blocksRaycast;
        return data;
    }

    bool UIComponentService::setUIRectData(EntityHandle entity, const UIRectData& rectData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIRectComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UIRectComponent>();
        comp.anchorMin = rectData.anchorMin;
        comp.anchorMax = rectData.anchorMax;
        comp.pivot = rectData.pivot;
        comp.sizeDelta = rectData.sizeDelta;
        comp.anchoredPosition = rectData.anchoredPosition;
        comp.blocksRaycast = rectData.blocksRaycast;
        return true;
    }

    bool UIComponentService::setUIRectPixels(EntityHandle entity, float x, float y, float w, float h) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        entt::entity e = internal::fromHandle(entity);
        scene::Entity sceneEntity(e);
        if (!sceneEntity.hasComponent<components::UIRectComponent>()) {
            return false;
        }

        auto& dispatcher = ::events::EventDispatcher::instance();
        float vw = static_cast<float>(dispatcher.query(::events::application::GetViewportWidthQuery{}));
        float vh = static_cast<float>(dispatcher.query(::events::application::GetViewportHeightQuery{}));
        if (vw <= 0.0f || vh <= 0.0f) {
            return false;
        }

        // Express the rect purely through NORMALIZED anchors (sizeDelta and
        // anchoredPosition zeroed). resolvePixelRect then lands on the same
        // normalized rect whatever extent the UI pass renders against — in the
        // editor the play panel (mouse space, what vw/vh report) and the UI
        // render extent (swapchain) differ, and absolute canvas-unit math would
        // shift the rect. Normalized anchors are extent- and canvas-scale-
        // invariant, exactly like the picker's normalized ray math.
        // Note: this overwrites the authored anchors/pivot — setRectPixels is
        // for fully script-driven overlays (drag boxes), not authored layout.
        auto& comp = sceneEntity.getComponent<components::UIRectComponent>();
        comp.anchorMin = glm::vec2(x / vw, 1.0f - (y + h) / vh);
        comp.anchorMax = glm::vec2((x + w) / vw, 1.0f - y / vh);
        comp.pivot = glm::vec2(0.5f, 0.5f);
        comp.sizeDelta = glm::vec2(0.0f, 0.0f);
        comp.anchoredPosition = glm::vec2(0.0f, 0.0f);
        return true;
    }

    std::optional<UIResolvedRectData> UIComponentService::getUIResolvedRectPixels(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        entt::entity e = internal::fromHandle(entity);
        scene::Entity sceneEntity(e);
        if (!sceneEntity.hasComponent<components::UIRectComponent>()) {
            return std::nullopt;
        }

        auto& dispatcher = ::events::EventDispatcher::instance();
        float vw = static_cast<float>(dispatcher.query(::events::application::GetViewportWidthQuery{}));
        float vh = static_cast<float>(dispatcher.query(::events::application::GetViewportHeightQuery{}));
        if (vw <= 0.0f || vh <= 0.0f) {
            return std::nullopt;
        }

        // Same resolution path as the runtime UI hit tests (UIInteractionSystem):
        // canvas scale + resolvePixelRect against the mouse-space viewport, so the
        // result lines up with Input::getViewportMouseX/Y and setUIRectPixels.
        const auto& rectComp = sceneEntity.getComponent<components::UIRectComponent>();
        const auto* canvas = utilities::ui::findCanvasForEntity(registry, e);
        float scale = utilities::ui::computeCanvasScale(canvas, vw, vh);
        utilities::ui::PixelRect rect = utilities::ui::resolvePixelRect(rectComp, vw, vh, scale);

        UIResolvedRectData data;
        data.x = rect.x;
        data.y = rect.y;
        data.w = rect.w;
        data.h = rect.h;
        return data;
    }

    // ========== UI Image Operations ==========

    bool UIComponentService::addUIImageComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        if (sceneEntity.hasComponent<components::UIImageComponent>()) {
            return false;
        }

        sceneEntity.addComponent<components::UIImageComponent>();
        return true;
    }

    bool UIComponentService::removeUIImageComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIImageComponent>()) {
            return false;
        }

        sceneEntity.removeComponent<components::UIImageComponent>();
        return true;
    }

    bool UIComponentService::hasUIImageComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::UIImageComponent>();
    }

    std::optional<UIImageData> UIComponentService::getUIImageData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIImageComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::UIImageComponent>();

        UIImageData data;
        data.textureRef = comp.textureRef;
        data.colorTint = comp.colorTint;
        if (comp.renderTextureSource != entt::null)
            data.renderTextureSource = EntityHandle{static_cast<uint64_t>(comp.renderTextureSource)};
        else
            data.renderTextureSource = EntityHandle::invalid();
        data.renderTextureSourceName = comp.renderTextureSourceName;
        data.imageType = static_cast<uint8_t>(comp.imageType);
        data.border = comp.border;
        data.sourceWidth = comp.sourceWidth;
        data.sourceHeight = comp.sourceHeight;
        return data;
    }

    bool UIComponentService::setUIImageData(EntityHandle entity, const UIImageData& imageData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIImageComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UIImageComponent>();
        comp.textureRef = imageData.textureRef;
        comp.colorTint = imageData.colorTint;
        comp.renderTextureSourceName = imageData.renderTextureSourceName;
        comp.imageType = static_cast<components::UIImageType>(imageData.imageType);
        comp.border = imageData.border;
        comp.sourceWidth = imageData.sourceWidth;
        comp.sourceHeight = imageData.sourceHeight;

        // Resolve renderTextureSourceName → entity handle
        comp.renderTextureSource = entt::null;
        if (!comp.renderTextureSourceName.empty()) {
            auto nameView = registry.view<components::NameComponent, components::RenderTextureComponent>();
            for (auto e : nameView) {
                if (nameView.get<components::NameComponent>(e).name == comp.renderTextureSourceName) {
                    comp.renderTextureSource = e;
                    break;
                }
            }
        }
        return true;
    }

    // ========== Event Handler Registration ==========

    void UIComponentService::registerCanvasRectImageHandlers(events::EventDispatcher& dispatcher) {
        // Canvas commands
        dispatcher.registerCommandHandler<events::ui::AddUICanvasComponentCommand>(
            [this](const events::ui::AddUICanvasComponentCommand& cmd) {
                return addUICanvasComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::RemoveUICanvasComponentCommand>(
            [this](const events::ui::RemoveUICanvasComponentCommand& cmd) {
                return removeUICanvasComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::SetUICanvasDataCommand>(
            [this](const events::ui::SetUICanvasDataCommand& cmd) {
                return setUICanvasData(cmd.entity, cmd.canvasData);
            });

        dispatcher.registerCommandHandler<events::ui::MarkUIPreviewSandboxCommand>(
            [this](const events::ui::MarkUIPreviewSandboxCommand& cmd) {
                return markUIPreviewSandbox(cmd.entity, cmd.tagged);
            });

        dispatcher.registerQueryHandler<events::ui::HasUICanvasComponentQuery>(
            [this](const events::ui::HasUICanvasComponentQuery& query) {
                return hasUICanvasComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUICanvasDataQuery>(
            [this](const events::ui::GetUICanvasDataQuery& query) {
                return getUICanvasData(query.entity);
            });

        // Rect commands
        dispatcher.registerCommandHandler<events::ui::AddUIRectComponentCommand>(
            [this](const events::ui::AddUIRectComponentCommand& cmd) {
                return addUIRectComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::RemoveUIRectComponentCommand>(
            [this](const events::ui::RemoveUIRectComponentCommand& cmd) {
                return removeUIRectComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::SetUIRectDataCommand>(
            [this](const events::ui::SetUIRectDataCommand& cmd) {
                return setUIRectData(cmd.entity, cmd.rectData);
            });

        dispatcher.registerCommandHandler<events::ui::SetUIRectPixelsCommand>(
            [this](const events::ui::SetUIRectPixelsCommand& cmd) {
                return setUIRectPixels(cmd.entity, cmd.x, cmd.y, cmd.w, cmd.h);
            });

        dispatcher.registerQueryHandler<events::ui::HasUIRectComponentQuery>(
            [this](const events::ui::HasUIRectComponentQuery& query) {
                return hasUIRectComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUIRectDataQuery>(
            [this](const events::ui::GetUIRectDataQuery& query) {
                return getUIRectData(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUIResolvedRectQuery>(
            [this](const events::ui::GetUIResolvedRectQuery& query) {
                return getUIResolvedRectPixels(query.entity);
            });

        // Image commands
        dispatcher.registerCommandHandler<events::ui::AddUIImageComponentCommand>(
            [this](const events::ui::AddUIImageComponentCommand& cmd) {
                return addUIImageComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::RemoveUIImageComponentCommand>(
            [this](const events::ui::RemoveUIImageComponentCommand& cmd) {
                return removeUIImageComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::SetUIImageDataCommand>(
            [this](const events::ui::SetUIImageDataCommand& cmd) {
                return setUIImageData(cmd.entity, cmd.imageData);
            });

        dispatcher.registerQueryHandler<events::ui::HasUIImageComponentQuery>(
            [this](const events::ui::HasUIImageComponentQuery& query) {
                return hasUIImageComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUIImageDataQuery>(
            [this](const events::ui::GetUIImageDataQuery& query) {
                return getUIImageData(query.entity);
            });
    }

}

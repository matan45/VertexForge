#include "UIComponentService.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/UIEvents.hpp"

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
        return true;
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
        data.texturePath = comp.texturePath;
        data.colorTint = comp.colorTint;
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
        comp.texturePath = imageData.texturePath;
        comp.colorTint = imageData.colorTint;
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

        dispatcher.registerQueryHandler<events::ui::HasUIRectComponentQuery>(
            [this](const events::ui::HasUIRectComponentQuery& query) {
                return hasUIRectComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUIRectDataQuery>(
            [this](const events::ui::GetUIRectDataQuery& query) {
                return getUIRectData(query.entity);
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

#include "UIComponentService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/UIEvents.hpp"

namespace services {

    UIComponentService::UIComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(std::move(sceneGraph)) {}

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

    // ========== UI Scroll Operations ==========

    bool UIComponentService::addUIScrollComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        if (sceneEntity.hasComponent<components::UIScrollComponent>()) {
            return false;
        }

        sceneEntity.addComponent<components::UIScrollComponent>();

        // Auto-add UIRectComponent if missing (scroll needs rect for viewport bounds)
        if (!sceneEntity.hasComponent<components::UIRectComponent>()) {
            sceneEntity.addComponent<components::UIRectComponent>();
        }

        return true;
    }

    bool UIComponentService::removeUIScrollComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIScrollComponent>()) {
            return false;
        }

        sceneEntity.removeComponent<components::UIScrollComponent>();
        return true;
    }

    bool UIComponentService::hasUIScrollComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::UIScrollComponent>();
    }

    std::optional<UIScrollData> UIComponentService::getUIScrollData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIScrollComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::UIScrollComponent>();

        UIScrollData data;
        data.horizontalScrollEnabled = comp.horizontalScrollEnabled;
        data.verticalScrollEnabled = comp.verticalScrollEnabled;
        data.horizontalScrollbarVisibility = static_cast<uint8_t>(comp.horizontalScrollbarVisibility);
        data.verticalScrollbarVisibility = static_cast<uint8_t>(comp.verticalScrollbarVisibility);
        data.scrollSensitivity = comp.scrollSensitivity;
        return data;
    }

    bool UIComponentService::setUIScrollData(EntityHandle entity, const UIScrollData& scrollData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIScrollComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UIScrollComponent>();
        comp.horizontalScrollEnabled = scrollData.horizontalScrollEnabled;
        comp.verticalScrollEnabled = scrollData.verticalScrollEnabled;
        comp.horizontalScrollbarVisibility = static_cast<components::ScrollbarVisibility>(scrollData.horizontalScrollbarVisibility);
        comp.verticalScrollbarVisibility = static_cast<components::ScrollbarVisibility>(scrollData.verticalScrollbarVisibility);
        comp.scrollSensitivity = scrollData.scrollSensitivity;
        return true;
    }

    bool UIComponentService::setScrollOffset(EntityHandle entity, const glm::vec2& offset) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIScrollComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UIScrollComponent>();
        comp.scrollOffset = offset;
        return true;
    }

    std::optional<glm::vec2> UIComponentService::getScrollOffset(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIScrollComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::UIScrollComponent>();
        return comp.scrollOffset;
    }

    // ========== UI Layout Group CRUD ==========

    bool UIComponentService::addUILayoutGroupComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        if (sceneEntity.hasComponent<components::UILayoutGroupComponent>()) {
            return false;
        }

        sceneEntity.addComponent<components::UILayoutGroupComponent>();

        if (!sceneEntity.hasComponent<components::UIRectComponent>()) {
            sceneEntity.addComponent<components::UIRectComponent>();
        }

        if (!sceneEntity.hasComponent<components::ChildrenComponent>()) {
            sceneEntity.addComponent<components::ChildrenComponent>();
        }

        return true;
    }

    bool UIComponentService::removeUILayoutGroupComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UILayoutGroupComponent>()) {
            return false;
        }

        sceneEntity.removeComponent<components::UILayoutGroupComponent>();
        return true;
    }

    bool UIComponentService::hasUILayoutGroupComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::UILayoutGroupComponent>();
    }

    std::optional<UILayoutGroupData> UIComponentService::getUILayoutGroupData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UILayoutGroupComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::UILayoutGroupComponent>();

        UILayoutGroupData data;
        data.direction = static_cast<uint8_t>(comp.direction);
        data.spacing = comp.spacing;
        data.padding = comp.padding;
        data.childAlignment = static_cast<uint8_t>(comp.childAlignment);
        data.constraintCount = comp.constraintCount;
        return data;
    }

    bool UIComponentService::setUILayoutGroupData(EntityHandle entity, const UILayoutGroupData& layoutGroupData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UILayoutGroupComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UILayoutGroupComponent>();
        comp.direction = static_cast<components::LayoutDirection>(layoutGroupData.direction);
        comp.spacing = layoutGroupData.spacing;
        comp.padding = layoutGroupData.padding;
        comp.childAlignment = static_cast<components::ChildAlignment>(layoutGroupData.childAlignment);
        comp.constraintCount = std::max(1, layoutGroupData.constraintCount);
        return true;
    }

    // ========== UI Label CRUD ==========

    bool UIComponentService::addUILabelComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        if (sceneEntity.hasComponent<components::UILabelComponent>()) {
            return false;
        }

        sceneEntity.addComponent<components::UILabelComponent>();

        // Auto-add UIRectComponent if missing (label needs rect for positioning)
        if (!sceneEntity.hasComponent<components::UIRectComponent>()) {
            sceneEntity.addComponent<components::UIRectComponent>();
        }

        return true;
    }

    bool UIComponentService::removeUILabelComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UILabelComponent>()) {
            return false;
        }

        sceneEntity.removeComponent<components::UILabelComponent>();
        return true;
    }

    bool UIComponentService::hasUILabelComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::UILabelComponent>();
    }

    std::optional<UILabelData> UIComponentService::getUILabelData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UILabelComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::UILabelComponent>();

        UILabelData data;
        data.text = comp.text;
        data.fontPath = comp.fontPath;
        data.fontSize = comp.fontSize;
        data.fontStyle = static_cast<uint8_t>(comp.fontStyle);
        data.color = comp.color;
        data.horizontalAlignment = static_cast<uint8_t>(comp.horizontalAlignment);
        data.verticalAlignment = static_cast<uint8_t>(comp.verticalAlignment);
        data.overflow = static_cast<uint8_t>(comp.overflow);
        data.wordWrap = comp.wordWrap;
        data.lineSpacing = comp.lineSpacing;
        data.letterSpacing = comp.letterSpacing;
        return data;
    }

    bool UIComponentService::setUILabelData(EntityHandle entity, const UILabelData& labelData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UILabelComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UILabelComponent>();
        comp.text = labelData.text;
        comp.fontPath = labelData.fontPath;
        comp.fontSize = labelData.fontSize;
        comp.fontStyle = static_cast<components::FontStyle>(labelData.fontStyle);
        comp.color = labelData.color;
        comp.horizontalAlignment = static_cast<components::HorizontalAlignment>(labelData.horizontalAlignment);
        comp.verticalAlignment = static_cast<components::VerticalAlignment>(labelData.verticalAlignment);
        comp.overflow = static_cast<components::TextOverflow>(labelData.overflow);
        comp.wordWrap = labelData.wordWrap;
        comp.lineSpacing = labelData.lineSpacing;
        comp.letterSpacing = labelData.letterSpacing;
        return true;
    }

    std::optional<glm::vec2> UIComponentService::getUILabelPreferredSize(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UILabelComponent>()) {
            return std::nullopt;
        }

        // Stub: actual preferred size calculation requires font metrics
        // Will be implemented in VK-428 (Frame Preparation)
        return std::nullopt;
    }

    // ========== UI Button CRUD ==========

    bool UIComponentService::addUIButtonComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        if (sceneEntity.hasComponent<components::UIButtonComponent>()) {
            return false;
        }

        sceneEntity.addComponent<components::UIButtonComponent>();

        // Auto-add UIRectComponent if missing (button needs rect for layout/hit-testing)
        if (!sceneEntity.hasComponent<components::UIRectComponent>()) {
            sceneEntity.addComponent<components::UIRectComponent>();
        }

        // Auto-add UIImageComponent if missing (button background)
        if (!sceneEntity.hasComponent<components::UIImageComponent>()) {
            sceneEntity.addComponent<components::UIImageComponent>();
        }

        // Auto-add UILabelComponent if missing (button text)
        if (!sceneEntity.hasComponent<components::UILabelComponent>()) {
            auto& labelComp = sceneEntity.addComponent<components::UILabelComponent>();
            labelComp.text = "Button";
            labelComp.horizontalAlignment = components::HorizontalAlignment::Center;
            labelComp.verticalAlignment = components::VerticalAlignment::Middle;
        }

        return true;
    }

    bool UIComponentService::removeUIButtonComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIButtonComponent>()) {
            return false;
        }

        sceneEntity.removeComponent<components::UIButtonComponent>();
        return true;
    }

    bool UIComponentService::hasUIButtonComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::UIButtonComponent>();
    }

    std::optional<UIButtonData> UIComponentService::getUIButtonData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIButtonComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::UIButtonComponent>();

        UIButtonData data;
        data.normalColor = comp.normalColor;
        data.hoveredColor = comp.hoveredColor;
        data.pressedColor = comp.pressedColor;
        data.disabledColor = comp.disabledColor;
        data.normalTexture = comp.normalTexture;
        data.hoverTexture = comp.hoverTexture;
        data.pressedTexture = comp.pressedTexture;
        data.disabledTexture = comp.disabledTexture;
        data.colorTransitionDuration = comp.colorTransitionDuration;
        data.interactable = comp.interactable;
        data.currentState = static_cast<uint8_t>(comp.currentState);
        return data;
    }

    bool UIComponentService::setUIButtonData(EntityHandle entity, const UIButtonData& buttonData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIButtonComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UIButtonComponent>();
        comp.normalColor = buttonData.normalColor;
        comp.hoveredColor = buttonData.hoveredColor;
        comp.pressedColor = buttonData.pressedColor;
        comp.disabledColor = buttonData.disabledColor;
        comp.normalTexture = buttonData.normalTexture;
        comp.hoverTexture = buttonData.hoverTexture;
        comp.pressedTexture = buttonData.pressedTexture;
        comp.disabledTexture = buttonData.disabledTexture;
        comp.colorTransitionDuration = buttonData.colorTransitionDuration;
        comp.interactable = buttonData.interactable;
        comp.currentState = static_cast<components::UIButtonState>(buttonData.currentState);
        return true;
    }

    // ========== UI TextInput CRUD ==========

    bool UIComponentService::addUITextInputComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        if (sceneEntity.hasComponent<components::UITextInputComponent>()) {
            return false;
        }

        sceneEntity.addComponent<components::UITextInputComponent>();

        // Auto-add UIRectComponent if missing (text input needs rect for layout/hit-testing)
        if (!sceneEntity.hasComponent<components::UIRectComponent>()) {
            sceneEntity.addComponent<components::UIRectComponent>();
        }

        // Auto-add UIImageComponent if missing (text input background)
        if (!sceneEntity.hasComponent<components::UIImageComponent>()) {
            sceneEntity.addComponent<components::UIImageComponent>();
        }

        return true;
    }

    bool UIComponentService::removeUITextInputComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UITextInputComponent>()) {
            return false;
        }

        sceneEntity.removeComponent<components::UITextInputComponent>();
        return true;
    }

    bool UIComponentService::hasUITextInputComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::UITextInputComponent>();
    }

    std::optional<UITextInputData> UIComponentService::getUITextInputData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UITextInputComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::UITextInputComponent>();

        UITextInputData data;
        data.text = comp.text;
        data.placeholderText = comp.placeholderText;
        data.fontPath = comp.fontPath;
        data.fontSize = comp.fontSize;
        data.textColor = comp.textColor;
        data.placeholderColor = comp.placeholderColor;
        data.normalColor = comp.normalColor;
        data.hoveredColor = comp.hoveredColor;
        data.focusedColor = comp.focusedColor;
        data.disabledColor = comp.disabledColor;
        data.colorTransitionDuration = comp.colorTransitionDuration;
        data.interactable = comp.interactable;
        data.maxLength = comp.maxLength;
        data.selectionColor = comp.selectionColor;
        data.caretColor = comp.caretColor;
        data.caretWidth = comp.caretWidth;
        data.caretBlinkRate = comp.caretBlinkRate;
        data.currentState = static_cast<uint8_t>(comp.currentState);
        return data;
    }

    bool UIComponentService::setUITextInputData(EntityHandle entity, const UITextInputData& textInputData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UITextInputComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UITextInputComponent>();
        comp.text = textInputData.text;
        comp.placeholderText = textInputData.placeholderText;
        comp.fontPath = textInputData.fontPath;
        comp.fontSize = textInputData.fontSize;
        comp.textColor = textInputData.textColor;
        comp.placeholderColor = textInputData.placeholderColor;
        comp.normalColor = textInputData.normalColor;
        comp.hoveredColor = textInputData.hoveredColor;
        comp.focusedColor = textInputData.focusedColor;
        comp.disabledColor = textInputData.disabledColor;
        comp.colorTransitionDuration = textInputData.colorTransitionDuration;
        comp.interactable = textInputData.interactable;
        comp.maxLength = textInputData.maxLength;
        comp.selectionColor = textInputData.selectionColor;
        comp.caretColor = textInputData.caretColor;
        comp.caretWidth = textInputData.caretWidth;
        comp.caretBlinkRate = textInputData.caretBlinkRate;
        comp.currentState = static_cast<components::UITextInputState>(textInputData.currentState);
        return true;
    }

    // ========== UI Checkbox CRUD ==========

    bool UIComponentService::addUICheckboxComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        if (sceneEntity.hasComponent<components::UICheckboxComponent>()) {
            return false;
        }

        sceneEntity.addComponent<components::UICheckboxComponent>();

        // Auto-add UIRectComponent if missing (checkbox needs rect for layout/hit-testing)
        if (!sceneEntity.hasComponent<components::UIRectComponent>()) {
            sceneEntity.addComponent<components::UIRectComponent>();
        }

        // Auto-add UIImageComponent if missing (checkbox background)
        if (!sceneEntity.hasComponent<components::UIImageComponent>()) {
            sceneEntity.addComponent<components::UIImageComponent>();
        }

        return true;
    }

    bool UIComponentService::removeUICheckboxComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UICheckboxComponent>()) {
            return false;
        }

        sceneEntity.removeComponent<components::UICheckboxComponent>();
        return true;
    }

    bool UIComponentService::hasUICheckboxComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::UICheckboxComponent>();
    }

    std::optional<UICheckboxData> UIComponentService::getUICheckboxData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UICheckboxComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::UICheckboxComponent>();

        UICheckboxData data;
        data.isChecked = comp.isChecked;
        data.groupName = comp.groupName;
        data.allowUncheck = comp.allowUncheck;
        data.uncheckedColor = comp.uncheckedColor;
        data.checkedColor = comp.checkedColor;
        data.hoveredColor = comp.hoveredColor;
        data.disabledColor = comp.disabledColor;
        data.uncheckedTexture = comp.uncheckedTexture;
        data.checkedTexture = comp.checkedTexture;
        data.hoveredTexture = comp.hoveredTexture;
        data.disabledTexture = comp.disabledTexture;
        data.colorTransitionDuration = comp.colorTransitionDuration;
        data.interactable = comp.interactable;
        data.labelToggle = comp.labelToggle;
        data.currentState = static_cast<uint8_t>(comp.currentState);
        return data;
    }

    bool UIComponentService::setUICheckboxData(EntityHandle entity, const UICheckboxData& checkboxData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UICheckboxComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UICheckboxComponent>();
        comp.isChecked = checkboxData.isChecked;
        comp.groupName = checkboxData.groupName;
        comp.allowUncheck = checkboxData.allowUncheck;
        comp.uncheckedColor = checkboxData.uncheckedColor;
        comp.checkedColor = checkboxData.checkedColor;
        comp.hoveredColor = checkboxData.hoveredColor;
        comp.disabledColor = checkboxData.disabledColor;
        comp.uncheckedTexture = checkboxData.uncheckedTexture;
        comp.checkedTexture = checkboxData.checkedTexture;
        comp.hoveredTexture = checkboxData.hoveredTexture;
        comp.disabledTexture = checkboxData.disabledTexture;
        comp.colorTransitionDuration = checkboxData.colorTransitionDuration;
        comp.interactable = checkboxData.interactable;
        comp.labelToggle = checkboxData.labelToggle;
        comp.currentState = static_cast<components::UICheckboxState>(checkboxData.currentState);
        return true;
    }

    // ========== UI Dropdown CRUD ==========

    bool UIComponentService::addUIDropdownComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        if (sceneEntity.hasComponent<components::UIDropdownComponent>()) {
            return false;
        }

        sceneEntity.addComponent<components::UIDropdownComponent>();

        // Auto-add UIRectComponent if missing (dropdown needs rect for layout/hit-testing)
        if (!sceneEntity.hasComponent<components::UIRectComponent>()) {
            sceneEntity.addComponent<components::UIRectComponent>();
        }

        // Auto-add UIImageComponent if missing (dropdown header background)
        if (!sceneEntity.hasComponent<components::UIImageComponent>()) {
            sceneEntity.addComponent<components::UIImageComponent>();
        }

        // Auto-add UILabelComponent if missing (displays selected text / placeholder)
        if (!sceneEntity.hasComponent<components::UILabelComponent>()) {
            auto& labelComp = sceneEntity.addComponent<components::UILabelComponent>();
            labelComp.text = "Select...";
            labelComp.horizontalAlignment = components::HorizontalAlignment::Left;
            labelComp.verticalAlignment = components::VerticalAlignment::Middle;
        }

        return true;
    }

    bool UIComponentService::removeUIDropdownComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIDropdownComponent>()) {
            return false;
        }

        // If this dropdown was the active one, clear the tracker
        auto enttEntity = internal::fromHandle(entity);
        if (components::UIDropdownComponent::activeDropdownEntity == enttEntity) {
            components::UIDropdownComponent::activeDropdownEntity = entt::null;
        }

        sceneEntity.removeComponent<components::UIDropdownComponent>();
        return true;
    }

    bool UIComponentService::hasUIDropdownComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::UIDropdownComponent>();
    }

    std::optional<UIDropdownData> UIComponentService::getUIDropdownData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIDropdownComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::UIDropdownComponent>();

        UIDropdownData data;
        data.options.reserve(comp.options.size());
        for (const auto& opt : comp.options) {
            data.options.push_back({opt.text, opt.iconPath});
        }
        data.selectedIndex = comp.selectedIndex;
        data.placeholderText = comp.placeholderText;
        data.maxVisibleItems = comp.maxVisibleItems;
        data.interactable = comp.interactable;
        data.normalColor = comp.normalColor;
        data.hoveredColor = comp.hoveredColor;
        data.openColor = comp.openColor;
        data.disabledColor = comp.disabledColor;
        data.listBackgroundColor = comp.listBackgroundColor;
        data.itemNormalColor = comp.itemNormalColor;
        data.itemHoveredColor = comp.itemHoveredColor;
        data.fontPath = comp.fontPath;
        data.fontSize = comp.fontSize;
        data.colorTransitionDuration = comp.colorTransitionDuration;
        data.currentState = static_cast<uint8_t>(comp.currentState);
        data.isOpen = comp.isOpen;
        data.hoveredOptionIndex = comp.hoveredOptionIndex;
        return data;
    }

    bool UIComponentService::setUIDropdownData(EntityHandle entity, const UIDropdownData& dropdownData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIDropdownComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UIDropdownComponent>();
        comp.options.clear();
        comp.options.reserve(dropdownData.options.size());
        for (const auto& opt : dropdownData.options) {
            comp.options.push_back({opt.text, opt.iconPath});
        }
        comp.selectedIndex = dropdownData.selectedIndex;
        comp.placeholderText = dropdownData.placeholderText;
        comp.maxVisibleItems = std::max(1, dropdownData.maxVisibleItems);
        comp.interactable = dropdownData.interactable;
        comp.normalColor = dropdownData.normalColor;
        comp.hoveredColor = dropdownData.hoveredColor;
        comp.openColor = dropdownData.openColor;
        comp.disabledColor = dropdownData.disabledColor;
        comp.listBackgroundColor = dropdownData.listBackgroundColor;
        comp.itemNormalColor = dropdownData.itemNormalColor;
        comp.itemHoveredColor = dropdownData.itemHoveredColor;
        comp.fontPath = dropdownData.fontPath;
        comp.fontSize = dropdownData.fontSize;
        comp.colorTransitionDuration = dropdownData.colorTransitionDuration;
        comp.currentState = static_cast<components::UIDropdownState>(dropdownData.currentState);
        return true;
    }

    bool UIComponentService::setUIDropdownSelectedIndex(EntityHandle entity, int selectedIndex) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIDropdownComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UIDropdownComponent>();
        int previousIndex = comp.selectedIndex;

        // Clamp to valid range
        if (selectedIndex >= static_cast<int>(comp.options.size())) {
            selectedIndex = static_cast<int>(comp.options.size()) - 1;
        }

        comp.selectedIndex = selectedIndex;

        if (previousIndex != selectedIndex) {
            auto& dispatcher = events::EventDispatcher::instance();

            std::string entityName;
            if (sceneEntity.hasComponent<components::NameComponent>()) {
                entityName = sceneEntity.getComponent<components::NameComponent>().name;
            }

            events::ui::UIDropdownSelectionChangedNotification notif;
            notif.entity = entity;
            notif.entityName = entityName;
            notif.previousIndex = previousIndex;
            notif.newIndex = selectedIndex;
            notif.selectedValue = (selectedIndex >= 0 && selectedIndex < static_cast<int>(comp.options.size()))
                                      ? comp.options[selectedIndex].text : "";
            dispatcher.publish(notif);
        }

        return true;
    }

    bool UIComponentService::openUIDropdown(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIDropdownComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UIDropdownComponent>();
        if (!comp.interactable || comp.isOpen) {
            return false;
        }

        // Close any currently active dropdown
        auto activeEntity = components::UIDropdownComponent::activeDropdownEntity;
        if (activeEntity != entt::null && registry.valid(activeEntity)) {
            if (registry.all_of<components::UIDropdownComponent>(activeEntity)) {
                auto& activeComp = registry.get<components::UIDropdownComponent>(activeEntity);
                activeComp.isOpen = false;
                activeComp.currentState = activeComp.interactable
                    ? components::UIDropdownState::Normal
                    : components::UIDropdownState::Disabled;

                // Publish closed notification for the old dropdown
                auto& dispatcher = events::EventDispatcher::instance();
                events::ui::UIDropdownClosedNotification closedNotif;
                closedNotif.entity = internal::toHandle(activeEntity);
                if (registry.all_of<components::NameComponent>(activeEntity)) {
                    closedNotif.entityName = registry.get<components::NameComponent>(activeEntity).name;
                }
                dispatcher.publish(closedNotif);
            }
        }

        comp.isOpen = true;
        comp.currentState = components::UIDropdownState::Open;
        components::UIDropdownComponent::activeDropdownEntity = internal::fromHandle(entity);

        // Publish opened notification
        auto& dispatcher = events::EventDispatcher::instance();
        std::string entityName;
        if (sceneEntity.hasComponent<components::NameComponent>()) {
            entityName = sceneEntity.getComponent<components::NameComponent>().name;
        }
        events::ui::UIDropdownOpenedNotification notif;
        notif.entity = entity;
        notif.entityName = entityName;
        dispatcher.publish(notif);

        return true;
    }

    bool UIComponentService::closeUIDropdown(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIDropdownComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UIDropdownComponent>();
        if (!comp.isOpen) {
            return false;
        }

        comp.isOpen = false;
        comp.hoveredOptionIndex = -1;
        comp.listScrollOffset = 0.0f;
        comp.currentState = comp.interactable ? components::UIDropdownState::Normal
                                              : components::UIDropdownState::Disabled;

        auto enttEntity = internal::fromHandle(entity);
        if (components::UIDropdownComponent::activeDropdownEntity == enttEntity) {
            components::UIDropdownComponent::activeDropdownEntity = entt::null;
        }

        // Publish closed notification
        auto& dispatcher = events::EventDispatcher::instance();
        std::string entityName;
        if (sceneEntity.hasComponent<components::NameComponent>()) {
            entityName = sceneEntity.getComponent<components::NameComponent>().name;
        }
        events::ui::UIDropdownClosedNotification notif;
        notif.entity = entity;
        notif.entityName = entityName;
        dispatcher.publish(notif);

        return true;
    }

    // ========== UI Tabs CRUD ==========

    bool UIComponentService::addUITabsComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        if (sceneEntity.hasComponent<components::UITabsComponent>()) {
            return false;
        }

        sceneEntity.addComponent<components::UITabsComponent>();

        // Auto-add UIRectComponent if missing
        if (!sceneEntity.hasComponent<components::UIRectComponent>()) {
            sceneEntity.addComponent<components::UIRectComponent>();
        }

        return true;
    }

    bool UIComponentService::removeUITabsComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UITabsComponent>()) {
            return false;
        }

        sceneEntity.removeComponent<components::UITabsComponent>();
        return true;
    }

    bool UIComponentService::hasUITabsComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::UITabsComponent>();
    }

    std::optional<UITabsData> UIComponentService::getUITabsData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UITabsComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::UITabsComponent>();

        UITabsData data;
        data.tabBarPosition = static_cast<uint8_t>(comp.tabBarPosition);
        data.activeTabIndex = comp.activeTabIndex;
        data.previousTabIndex = comp.previousTabIndex;
        return data;
    }

    bool UIComponentService::setUITabsData(EntityHandle entity, const UITabsData& tabsData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UITabsComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UITabsComponent>();
        comp.tabBarPosition = static_cast<components::TabBarPosition>(tabsData.tabBarPosition);
        comp.activeTabIndex = tabsData.activeTabIndex;
        comp.previousTabIndex = tabsData.previousTabIndex;
        return true;
    }

    bool UIComponentService::selectTab(EntityHandle entity, int tabIndex) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UITabsComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UITabsComponent>();

        // Store previous
        int previousTabIndex = comp.activeTabIndex;
        comp.previousTabIndex = previousTabIndex;
        comp.activeTabIndex = tabIndex;

        // Find tab bar (first child with UILayoutGroupComponent) and panels
        if (!sceneEntity.hasComponent<components::ChildrenComponent>()) {
            return false;
        }

        const auto& children = sceneEntity.getComponent<components::ChildrenComponent>().children;
        std::vector<entt::entity> panels;

        for (auto childEntity : children) {
            if (!registry.valid(childEntity)) continue;

            // Skip the tab bar child (first child with UILayoutGroupComponent)
            if (registry.all_of<components::UILayoutGroupComponent>(childEntity)) {
                continue;
            }

            panels.push_back(childEntity);
        }

        // Toggle panel visibility: only the panel at tabIndex is active
        for (int i = 0; i < static_cast<int>(panels.size()); ++i) {
            if (registry.all_of<components::NameComponent>(panels[i])) {
                auto& nameComp = registry.get<components::NameComponent>(panels[i]);
                nameComp.isActive = (i == tabIndex);
            }
        }

        // Get entity name for notifications
        std::string entityName;
        if (sceneEntity.hasComponent<components::NameComponent>()) {
            entityName = sceneEntity.getComponent<components::NameComponent>().name;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        // Always publish tab selected
        events::ui::UITabSelectedNotification selectedNotif;
        selectedNotif.entity = entity;
        selectedNotif.entityName = entityName;
        selectedNotif.tabIndex = tabIndex;
        dispatcher.publish(selectedNotif);

        // Only publish tab changed if actually changed
        if (previousTabIndex != tabIndex) {
            events::ui::UITabChangedNotification changedNotif;
            changedNotif.entity = entity;
            changedNotif.entityName = entityName;
            changedNotif.newTabIndex = tabIndex;
            changedNotif.previousTabIndex = previousTabIndex;
            dispatcher.publish(changedNotif);
        }

        return true;
    }

    // ========== Event Handler Registration ==========

    void UIComponentService::registerEventHandlers(events::EventDispatcher& dispatcher) {
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

        // Canvas queries
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

        // Rect queries
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

        // Image queries
        dispatcher.registerQueryHandler<events::ui::HasUIImageComponentQuery>(
            [this](const events::ui::HasUIImageComponentQuery& query) {
                return hasUIImageComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUIImageDataQuery>(
            [this](const events::ui::GetUIImageDataQuery& query) {
                return getUIImageData(query.entity);
            });

        // Scroll commands
        dispatcher.registerCommandHandler<events::ui::AddUIScrollComponentCommand>(
            [this](const events::ui::AddUIScrollComponentCommand& cmd) {
                return addUIScrollComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::RemoveUIScrollComponentCommand>(
            [this](const events::ui::RemoveUIScrollComponentCommand& cmd) {
                return removeUIScrollComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::SetUIScrollDataCommand>(
            [this](const events::ui::SetUIScrollDataCommand& cmd) {
                return setUIScrollData(cmd.entity, cmd.scrollData);
            });

        dispatcher.registerCommandHandler<events::ui::SetScrollOffsetCommand>(
            [this](const events::ui::SetScrollOffsetCommand& cmd) {
                return setScrollOffset(cmd.entity, cmd.offset);
            });

        // Scroll queries
        dispatcher.registerQueryHandler<events::ui::HasUIScrollComponentQuery>(
            [this](const events::ui::HasUIScrollComponentQuery& query) {
                return hasUIScrollComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUIScrollDataQuery>(
            [this](const events::ui::GetUIScrollDataQuery& query) {
                return getUIScrollData(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetScrollOffsetQuery>(
            [this](const events::ui::GetScrollOffsetQuery& query) {
                return getScrollOffset(query.entity);
            });

        // Layout Group commands
        dispatcher.registerCommandHandler<events::ui::AddUILayoutGroupComponentCommand>(
            [this](const events::ui::AddUILayoutGroupComponentCommand& cmd) {
                return addUILayoutGroupComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::RemoveUILayoutGroupComponentCommand>(
            [this](const events::ui::RemoveUILayoutGroupComponentCommand& cmd) {
                return removeUILayoutGroupComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::SetUILayoutGroupDataCommand>(
            [this](const events::ui::SetUILayoutGroupDataCommand& cmd) {
                return setUILayoutGroupData(cmd.entity, cmd.layoutGroupData);
            });

        // Layout Group queries
        dispatcher.registerQueryHandler<events::ui::HasUILayoutGroupComponentQuery>(
            [this](const events::ui::HasUILayoutGroupComponentQuery& query) {
                return hasUILayoutGroupComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUILayoutGroupDataQuery>(
            [this](const events::ui::GetUILayoutGroupDataQuery& query) {
                return getUILayoutGroupData(query.entity);
            });

        // Label commands
        dispatcher.registerCommandHandler<events::ui::AddUILabelComponentCommand>(
            [this](const events::ui::AddUILabelComponentCommand& cmd) {
                return addUILabelComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::RemoveUILabelComponentCommand>(
            [this](const events::ui::RemoveUILabelComponentCommand& cmd) {
                return removeUILabelComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::SetUILabelDataCommand>(
            [this](const events::ui::SetUILabelDataCommand& cmd) {
                return setUILabelData(cmd.entity, cmd.labelData);
            });

        // Label queries
        dispatcher.registerQueryHandler<events::ui::HasUILabelComponentQuery>(
            [this](const events::ui::HasUILabelComponentQuery& query) {
                return hasUILabelComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUILabelDataQuery>(
            [this](const events::ui::GetUILabelDataQuery& query) {
                return getUILabelData(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUILabelPreferredSizeQuery>(
            [this](const events::ui::GetUILabelPreferredSizeQuery& query) {
                return getUILabelPreferredSize(query.entity);
            });

        // Button commands
        dispatcher.registerCommandHandler<events::ui::AddUIButtonComponentCommand>(
            [this](const events::ui::AddUIButtonComponentCommand& cmd) {
                return addUIButtonComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::RemoveUIButtonComponentCommand>(
            [this](const events::ui::RemoveUIButtonComponentCommand& cmd) {
                return removeUIButtonComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::SetUIButtonDataCommand>(
            [this](const events::ui::SetUIButtonDataCommand& cmd) {
                return setUIButtonData(cmd.entity, cmd.buttonData);
            });

        // Button queries
        dispatcher.registerQueryHandler<events::ui::HasUIButtonComponentQuery>(
            [this](const events::ui::HasUIButtonComponentQuery& query) {
                return hasUIButtonComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUIButtonDataQuery>(
            [this](const events::ui::GetUIButtonDataQuery& query) {
                return getUIButtonData(query.entity);
            });

        // TextInput commands
        dispatcher.registerCommandHandler<events::ui::AddUITextInputComponentCommand>(
            [this](const events::ui::AddUITextInputComponentCommand& cmd) {
                return addUITextInputComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::RemoveUITextInputComponentCommand>(
            [this](const events::ui::RemoveUITextInputComponentCommand& cmd) {
                return removeUITextInputComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::SetUITextInputDataCommand>(
            [this](const events::ui::SetUITextInputDataCommand& cmd) {
                return setUITextInputData(cmd.entity, cmd.textInputData);
            });

        // TextInput queries
        dispatcher.registerQueryHandler<events::ui::HasUITextInputComponentQuery>(
            [this](const events::ui::HasUITextInputComponentQuery& query) {
                return hasUITextInputComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUITextInputDataQuery>(
            [this](const events::ui::GetUITextInputDataQuery& query) {
                return getUITextInputData(query.entity);
            });

        // Checkbox commands
        dispatcher.registerCommandHandler<events::ui::AddUICheckboxComponentCommand>(
            [this](const events::ui::AddUICheckboxComponentCommand& cmd) {
                return addUICheckboxComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::RemoveUICheckboxComponentCommand>(
            [this](const events::ui::RemoveUICheckboxComponentCommand& cmd) {
                return removeUICheckboxComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::SetUICheckboxDataCommand>(
            [this](const events::ui::SetUICheckboxDataCommand& cmd) {
                return setUICheckboxData(cmd.entity, cmd.checkboxData);
            });

        // Checkbox queries
        dispatcher.registerQueryHandler<events::ui::HasUICheckboxComponentQuery>(
            [this](const events::ui::HasUICheckboxComponentQuery& query) {
                return hasUICheckboxComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUICheckboxDataQuery>(
            [this](const events::ui::GetUICheckboxDataQuery& query) {
                return getUICheckboxData(query.entity);
            });

        // Dropdown commands
        dispatcher.registerCommandHandler<events::ui::AddUIDropdownComponentCommand>(
            [this](const events::ui::AddUIDropdownComponentCommand& cmd) {
                return addUIDropdownComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::RemoveUIDropdownComponentCommand>(
            [this](const events::ui::RemoveUIDropdownComponentCommand& cmd) {
                return removeUIDropdownComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::SetUIDropdownDataCommand>(
            [this](const events::ui::SetUIDropdownDataCommand& cmd) {
                return setUIDropdownData(cmd.entity, cmd.dropdownData);
            });

        dispatcher.registerCommandHandler<events::ui::SetUIDropdownSelectedIndexCommand>(
            [this](const events::ui::SetUIDropdownSelectedIndexCommand& cmd) {
                return setUIDropdownSelectedIndex(cmd.entity, cmd.selectedIndex);
            });

        dispatcher.registerCommandHandler<events::ui::OpenUIDropdownCommand>(
            [this](const events::ui::OpenUIDropdownCommand& cmd) {
                return openUIDropdown(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::CloseUIDropdownCommand>(
            [this](const events::ui::CloseUIDropdownCommand& cmd) {
                return closeUIDropdown(cmd.entity);
            });

        // Dropdown queries
        dispatcher.registerQueryHandler<events::ui::HasUIDropdownComponentQuery>(
            [this](const events::ui::HasUIDropdownComponentQuery& query) {
                return hasUIDropdownComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUIDropdownDataQuery>(
            [this](const events::ui::GetUIDropdownDataQuery& query) {
                return getUIDropdownData(query.entity);
            });

        // Tabs commands
        dispatcher.registerCommandHandler<events::ui::AddUITabsComponentCommand>(
            [this](const events::ui::AddUITabsComponentCommand& cmd) {
                return addUITabsComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::RemoveUITabsComponentCommand>(
            [this](const events::ui::RemoveUITabsComponentCommand& cmd) {
                return removeUITabsComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::SetUITabsDataCommand>(
            [this](const events::ui::SetUITabsDataCommand& cmd) {
                return setUITabsData(cmd.entity, cmd.tabsData);
            });

        dispatcher.registerCommandHandler<events::ui::SetUITabsActiveTabCommand>(
            [this](const events::ui::SetUITabsActiveTabCommand& cmd) {
                return selectTab(cmd.entity, cmd.tabIndex);
            });

        // Tabs queries
        dispatcher.registerQueryHandler<events::ui::HasUITabsComponentQuery>(
            [this](const events::ui::HasUITabsComponentQuery& query) {
                return hasUITabsComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUITabsDataQuery>(
            [this](const events::ui::GetUITabsDataQuery& query) {
                return getUITabsData(query.entity);
            });
    }

}

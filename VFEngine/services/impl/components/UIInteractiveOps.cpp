#include "UIComponentService.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/UIEvents.hpp"

namespace services {

    // ========== UI Label Operations ==========

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

    // ========== UI Button Operations ==========

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

    // ========== UI TextInput Operations ==========

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

    // ========== UI Checkbox Operations ==========

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

    // ========== Event Handler Registration ==========

    void UIComponentService::registerInteractiveHandlers(events::EventDispatcher& dispatcher) {
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

        dispatcher.registerQueryHandler<events::ui::HasUICheckboxComponentQuery>(
            [this](const events::ui::HasUICheckboxComponentQuery& query) {
                return hasUICheckboxComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUICheckboxDataQuery>(
            [this](const events::ui::GetUICheckboxDataQuery& query) {
                return getUICheckboxData(query.entity);
            });
    }

}

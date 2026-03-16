#include "UIComponentService.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/ui/UIEvents.hpp"

namespace services {

    // ========== UI Slider Operations ==========

    bool UIComponentService::addUISliderComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        if (sceneEntity.hasComponent<components::UISliderComponent>()) {
            return false;
        }

        sceneEntity.addComponent<components::UISliderComponent>();

        // Auto-add UIRectComponent if missing
        if (!sceneEntity.hasComponent<components::UIRectComponent>()) {
            sceneEntity.addComponent<components::UIRectComponent>();
        }

        // Auto-add UIImageComponent if missing (used for track background)
        if (!sceneEntity.hasComponent<components::UIImageComponent>()) {
            sceneEntity.addComponent<components::UIImageComponent>();
        }

        return true;
    }

    bool UIComponentService::removeUISliderComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UISliderComponent>()) {
            return false;
        }

        sceneEntity.removeComponent<components::UISliderComponent>();
        return true;
    }

    bool UIComponentService::hasUISliderComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::UISliderComponent>();
    }

    std::optional<UISliderData> UIComponentService::getUISliderData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UISliderComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::UISliderComponent>();

        UISliderData data;
        data.minValue = comp.minValue;
        data.maxValue = comp.maxValue;
        data.value = comp.value;
        data.stepSize = comp.stepSize;
        data.orientation = static_cast<uint8_t>(comp.orientation);
        data.clickTrackToSet = comp.clickTrackToSet;
        data.handleSizeRatio = comp.handleSizeRatio;
        data.handleNormalColor = comp.handleNormalColor;
        data.handleHoveredColor = comp.handleHoveredColor;
        data.handlePressedColor = comp.handlePressedColor;
        data.handleDisabledColor = comp.handleDisabledColor;
        data.handleNormalTextureRef = comp.handleNormalTextureRef;
        data.handleHoveredTextureRef = comp.handleHoveredTextureRef;
        data.handlePressedTextureRef = comp.handlePressedTextureRef;
        data.handleDisabledTextureRef = comp.handleDisabledTextureRef;
        data.fillColor = comp.fillColor;
        data.fillTextureRef = comp.fillTextureRef;
        data.colorTransitionDuration = comp.colorTransitionDuration;
        data.interactable = comp.interactable;
        data.currentState = static_cast<uint8_t>(comp.currentState);
        data.isDragging = comp.isDragging;
        return data;
    }

    bool UIComponentService::setUISliderData(EntityHandle entity, const UISliderData& sliderData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UISliderComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UISliderComponent>();
        comp.minValue = sliderData.minValue;
        comp.maxValue = sliderData.maxValue;
        comp.value = sliderData.value;
        comp.stepSize = sliderData.stepSize;
        comp.orientation = static_cast<components::UISliderOrientation>(sliderData.orientation);
        comp.clickTrackToSet = sliderData.clickTrackToSet;
        comp.handleSizeRatio = sliderData.handleSizeRatio;
        comp.handleNormalColor = sliderData.handleNormalColor;
        comp.handleHoveredColor = sliderData.handleHoveredColor;
        comp.handlePressedColor = sliderData.handlePressedColor;
        comp.handleDisabledColor = sliderData.handleDisabledColor;
        comp.handleNormalTextureRef = sliderData.handleNormalTextureRef;
        comp.handleHoveredTextureRef = sliderData.handleHoveredTextureRef;
        comp.handlePressedTextureRef = sliderData.handlePressedTextureRef;
        comp.handleDisabledTextureRef = sliderData.handleDisabledTextureRef;
        comp.fillColor = sliderData.fillColor;
        comp.fillTextureRef = sliderData.fillTextureRef;
        comp.colorTransitionDuration = sliderData.colorTransitionDuration;
        comp.interactable = sliderData.interactable;
        return true;
    }

    bool UIComponentService::setUISliderValue(EntityHandle entity, float value) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UISliderComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UISliderComponent>();
        float previousValue = comp.value;

        // Snap to step if stepSize > 0
        float newValue = value;
        if (comp.stepSize > 0.0f) {
            newValue = std::round((newValue - comp.minValue) / comp.stepSize) * comp.stepSize + comp.minValue;
        }

        // Clamp to [min, max]
        newValue = std::max(comp.minValue, std::min(newValue, comp.maxValue));
        comp.value = newValue;

        // Publish value changed notification if value actually changed
        if (newValue != previousValue) {
            std::string entityName;
            if (sceneEntity.hasComponent<components::NameComponent>()) {
                entityName = sceneEntity.getComponent<components::NameComponent>().name;
            }

            auto& dispatcher = events::EventDispatcher::instance();
            events::ui::UISliderValueChangedNotification notif;
            notif.entity = entity;
            notif.entityName = std::move(entityName);
            notif.newValue = newValue;
            notif.previousValue = previousValue;
            dispatcher.publish(notif);
        }

        return true;
    }

    // ========== UI ProgressBar Operations ==========

    bool UIComponentService::addUIProgressBarComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        if (sceneEntity.hasComponent<components::UIProgressBarComponent>()) {
            return false;
        }

        sceneEntity.addComponent<components::UIProgressBarComponent>();

        // Auto-add UIRectComponent if missing
        if (!sceneEntity.hasComponent<components::UIRectComponent>()) {
            sceneEntity.addComponent<components::UIRectComponent>();
        }

        return true;
    }

    bool UIComponentService::removeUIProgressBarComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIProgressBarComponent>()) {
            return false;
        }

        sceneEntity.removeComponent<components::UIProgressBarComponent>();
        return true;
    }

    bool UIComponentService::hasUIProgressBarComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::UIProgressBarComponent>();
    }

    std::optional<UIProgressBarData> UIComponentService::getUIProgressBarData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIProgressBarComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::UIProgressBarComponent>();

        UIProgressBarData data;
        data.minValue = comp.minValue;
        data.maxValue = comp.maxValue;
        data.value = comp.value;
        data.orientation = static_cast<uint8_t>(comp.orientation);
        data.invertDirection = comp.invertDirection;
        data.smoothInterpolation = comp.smoothInterpolation;
        data.interpolationSpeed = comp.interpolationSpeed;
        data.trackColor = comp.trackColor;
        data.trackTextureRef = comp.trackTextureRef;
        data.fillColor = comp.fillColor;
        data.fillTextureRef = comp.fillTextureRef;
        data.displayValue = comp.displayValue;
        return data;
    }

    bool UIComponentService::setUIProgressBarData(EntityHandle entity, const UIProgressBarData& progressBarData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIProgressBarComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UIProgressBarComponent>();
        comp.minValue = progressBarData.minValue;
        comp.maxValue = progressBarData.maxValue;
        comp.value = progressBarData.value;
        comp.orientation = static_cast<components::UISliderOrientation>(progressBarData.orientation);
        comp.invertDirection = progressBarData.invertDirection;
        comp.smoothInterpolation = progressBarData.smoothInterpolation;
        comp.interpolationSpeed = progressBarData.interpolationSpeed;
        comp.trackColor = progressBarData.trackColor;
        comp.trackTextureRef = progressBarData.trackTextureRef;
        comp.fillColor = progressBarData.fillColor;
        comp.fillTextureRef = progressBarData.fillTextureRef;
        return true;
    }

    bool UIComponentService::setUIProgressBarValue(EntityHandle entity, float value) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIProgressBarComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UIProgressBarComponent>();
        float previousValue = comp.value;

        // Clamp to [min, max]
        float newValue = std::max(comp.minValue, std::min(value, comp.maxValue));
        comp.value = newValue;

        // Publish value changed notification if value actually changed
        if (newValue != previousValue) {
            std::string entityName;
            if (sceneEntity.hasComponent<components::NameComponent>()) {
                entityName = sceneEntity.getComponent<components::NameComponent>().name;
            }

            auto& dispatcher = events::EventDispatcher::instance();
            events::ui::UIProgressBarValueChangedNotification notif;
            notif.entity = entity;
            notif.entityName = std::move(entityName);
            notif.newValue = newValue;
            notif.previousValue = previousValue;
            dispatcher.publish(notif);
        }

        // Completion notification logic
        if (newValue >= comp.maxValue && !comp.completedFired) {
            comp.completedFired = true;

            std::string entityName;
            if (sceneEntity.hasComponent<components::NameComponent>()) {
                entityName = sceneEntity.getComponent<components::NameComponent>().name;
            }

            auto& dispatcher = events::EventDispatcher::instance();
            events::ui::UIProgressBarCompletedNotification notif;
            notif.entity = entity;
            notif.entityName = std::move(entityName);
            dispatcher.publish(notif);
        } else if (newValue < comp.maxValue) {
            comp.completedFired = false;
        }

        return true;
    }

    // ========== Event Handler Registration ==========

    void UIComponentService::registerSliderProgressHandlers(events::EventDispatcher& dispatcher) {
        // Slider commands
        dispatcher.registerCommandHandler<events::ui::AddUISliderComponentCommand>(
            [this](const events::ui::AddUISliderComponentCommand& cmd) {
                return addUISliderComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::RemoveUISliderComponentCommand>(
            [this](const events::ui::RemoveUISliderComponentCommand& cmd) {
                return removeUISliderComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::SetUISliderDataCommand>(
            [this](const events::ui::SetUISliderDataCommand& cmd) {
                return setUISliderData(cmd.entity, cmd.sliderData);
            });

        dispatcher.registerCommandHandler<events::ui::SetUISliderValueCommand>(
            [this](const events::ui::SetUISliderValueCommand& cmd) {
                return setUISliderValue(cmd.entity, cmd.value);
            });

        dispatcher.registerQueryHandler<events::ui::HasUISliderComponentQuery>(
            [this](const events::ui::HasUISliderComponentQuery& query) {
                return hasUISliderComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUISliderDataQuery>(
            [this](const events::ui::GetUISliderDataQuery& query) {
                return getUISliderData(query.entity);
            });

        // ProgressBar commands
        dispatcher.registerCommandHandler<events::ui::AddUIProgressBarComponentCommand>(
            [this](const events::ui::AddUIProgressBarComponentCommand& cmd) {
                return addUIProgressBarComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::RemoveUIProgressBarComponentCommand>(
            [this](const events::ui::RemoveUIProgressBarComponentCommand& cmd) {
                return removeUIProgressBarComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::SetUIProgressBarDataCommand>(
            [this](const events::ui::SetUIProgressBarDataCommand& cmd) {
                return setUIProgressBarData(cmd.entity, cmd.progressBarData);
            });

        dispatcher.registerCommandHandler<events::ui::SetUIProgressBarValueCommand>(
            [this](const events::ui::SetUIProgressBarValueCommand& cmd) {
                return setUIProgressBarValue(cmd.entity, cmd.value);
            });

        dispatcher.registerQueryHandler<events::ui::HasUIProgressBarComponentQuery>(
            [this](const events::ui::HasUIProgressBarComponentQuery& query) {
                return hasUIProgressBarComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUIProgressBarDataQuery>(
            [this](const events::ui::GetUIProgressBarDataQuery& query) {
                return getUIProgressBarData(query.entity);
            });
    }

}

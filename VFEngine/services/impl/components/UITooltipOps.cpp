#include "UIComponentService.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/ui/UITooltipEvents.hpp"

namespace services {

    bool UIComponentService::addUITooltipComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;
        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::UITooltipComponent>()) return false;
        sceneEntity.addComponent<components::UITooltipComponent>();
        if (!sceneEntity.hasComponent<components::UIRectComponent>())
            sceneEntity.addComponent<components::UIRectComponent>();
        return true;
    }

    bool UIComponentService::removeUITooltipComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;
        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UITooltipComponent>()) return false;
        sceneEntity.removeComponent<components::UITooltipComponent>();
        return true;
    }

    bool UIComponentService::hasUITooltipComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;
        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::UITooltipComponent>();
    }

    std::optional<UITooltipData> UIComponentService::getUITooltipData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return std::nullopt;
        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UITooltipComponent>()) return std::nullopt;
        const auto& comp = sceneEntity.getComponent<components::UITooltipComponent>();
        UITooltipData data;
        data.mode = static_cast<uint8_t>(comp.mode);
        data.text = comp.text;
        data.showDelay = comp.showDelay;
        data.followCursor = comp.followCursor;
        data.offset = comp.offset;
        data.maxWidth = comp.maxWidth;
        data.backgroundColor = comp.backgroundColor;
        data.textColor = comp.textColor;
        data.fontRef = comp.fontRef;
        data.fontSize = comp.fontSize;
        data.padding = comp.padding;
        data.enabled = comp.enabled;
        data.panelChildName = comp.panelChildName;
        return data;
    }

    bool UIComponentService::setUITooltipData(EntityHandle entity, const UITooltipData& data) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;
        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UITooltipComponent>()) return false;
        auto& comp = sceneEntity.getComponent<components::UITooltipComponent>();
        comp.mode = static_cast<components::UITooltipMode>(data.mode);
        comp.text = data.text;
        comp.showDelay = data.showDelay;
        comp.followCursor = data.followCursor;
        comp.offset = data.offset;
        comp.maxWidth = data.maxWidth;
        comp.backgroundColor = data.backgroundColor;
        comp.textColor = data.textColor;
        comp.fontRef = data.fontRef;
        comp.fontSize = data.fontSize;
        comp.padding = data.padding;
        comp.enabled = data.enabled;
        comp.panelChildName = data.panelChildName;
        return true;
    }

    void UIComponentService::registerTooltipHandlers(events::EventDispatcher& dispatcher) {
        dispatcher.registerCommandHandler<events::ui::AddUITooltipComponentCommand>(
            [this](const events::ui::AddUITooltipComponentCommand& cmd) {
                return addUITooltipComponent(cmd.entity);
            });
        dispatcher.registerCommandHandler<events::ui::RemoveUITooltipComponentCommand>(
            [this](const events::ui::RemoveUITooltipComponentCommand& cmd) {
                return removeUITooltipComponent(cmd.entity);
            });
        dispatcher.registerCommandHandler<events::ui::SetUITooltipDataCommand>(
            [this](const events::ui::SetUITooltipDataCommand& cmd) {
                return setUITooltipData(cmd.entity, cmd.tooltipData);
            });
        dispatcher.registerQueryHandler<events::ui::HasUITooltipComponentQuery>(
            [this](const events::ui::HasUITooltipComponentQuery& query) {
                return hasUITooltipComponent(query.entity);
            });
        dispatcher.registerQueryHandler<events::ui::GetUITooltipDataQuery>(
            [this](const events::ui::GetUITooltipDataQuery& query) {
                return getUITooltipData(query.entity);
            });
    }

}

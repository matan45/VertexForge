#include "UIComponentService.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/ui/UIEvents.hpp"

namespace services {

    bool UIComponentService::addUIMaskComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;
        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::UIMaskComponent>()) return false;
        sceneEntity.addComponent<components::UIMaskComponent>();
        if (!sceneEntity.hasComponent<components::UIRectComponent>())
            sceneEntity.addComponent<components::UIRectComponent>();
        return true;
    }

    bool UIComponentService::removeUIMaskComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;
        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIMaskComponent>()) return false;
        sceneEntity.removeComponent<components::UIMaskComponent>();
        return true;
    }

    bool UIComponentService::hasUIMaskComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;
        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::UIMaskComponent>();
    }

    std::optional<UIMaskData> UIComponentService::getUIMaskData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return std::nullopt;
        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIMaskComponent>()) return std::nullopt;
        const auto& comp = sceneEntity.getComponent<components::UIMaskComponent>();
        UIMaskData data;
        data.maskMode = static_cast<uint8_t>(comp.maskMode);
        data.maskTexturePath = comp.maskTexturePath;
        data.alphaThreshold = comp.alphaThreshold;
        data.showMaskGraphic = comp.showMaskGraphic;
        return data;
    }

    bool UIComponentService::setUIMaskData(EntityHandle entity, const UIMaskData& data) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;
        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIMaskComponent>()) return false;
        auto& comp = sceneEntity.getComponent<components::UIMaskComponent>();
        comp.maskMode = static_cast<components::UIMaskMode>(data.maskMode);
        comp.maskTexturePath = data.maskTexturePath;
        comp.alphaThreshold = data.alphaThreshold;
        comp.showMaskGraphic = data.showMaskGraphic;
        return true;
    }

    void UIComponentService::registerMaskHandlers(events::EventDispatcher& dispatcher) {
        dispatcher.registerCommandHandler<events::ui::AddUIMaskComponentCommand>(
            [this](const events::ui::AddUIMaskComponentCommand& cmd) {
                return addUIMaskComponent(cmd.entity);
            });
        dispatcher.registerCommandHandler<events::ui::RemoveUIMaskComponentCommand>(
            [this](const events::ui::RemoveUIMaskComponentCommand& cmd) {
                return removeUIMaskComponent(cmd.entity);
            });
        dispatcher.registerCommandHandler<events::ui::SetUIMaskDataCommand>(
            [this](const events::ui::SetUIMaskDataCommand& cmd) {
                return setUIMaskData(cmd.entity, cmd.maskData);
            });
        dispatcher.registerQueryHandler<events::ui::HasUIMaskComponentQuery>(
            [this](const events::ui::HasUIMaskComponentQuery& query) {
                return hasUIMaskComponent(query.entity);
            });
        dispatcher.registerQueryHandler<events::ui::GetUIMaskDataQuery>(
            [this](const events::ui::GetUIMaskDataQuery& query) {
                return getUIMaskData(query.entity);
            });
    }

}

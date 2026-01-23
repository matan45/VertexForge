#include "IBLComponentService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/SceneEvents.hpp"

namespace services {

    IBLComponentService::IBLComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(std::move(sceneGraph)) {}

    std::optional<IBLData> IBLComponentService::getIBLData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::IBLComponent>()) {
            return std::nullopt;
        }

        auto& comp = sceneEntity.getComponent<components::IBLComponent>();
        IBLData data;
        data.fileName = comp.fileName;

        return data;
    }

    bool IBLComponentService::setIBLData(EntityHandle entity, const IBLData& ibl) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::IBLComponent>()) {
            auto& comp = sceneEntity.getComponent<components::IBLComponent>();
            comp.fileName = ibl.fileName;
        }
        else {
            sceneEntity.addComponent<components::IBLComponent>(ibl.fileName);
        }

        return true;
    }

    bool IBLComponentService::removeIBLComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::IBLComponent>()) {
            sceneEntity.removeComponent<components::IBLComponent>();
            return true;
        }

        return false;
    }

    bool IBLComponentService::hasIBLComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::IBLComponent>();
    }

    void IBLComponentService::registerEventHandlers(events::EventDispatcher& dispatcher) {
        dispatcher.registerCommandHandler<events::scene::SetIBLDataCommand>(
            [this](const events::scene::SetIBLDataCommand& cmd) {
                return setIBLData(cmd.entity, cmd.iblData);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveIBLComponentCommand>(
            [this](const events::scene::RemoveIBLComponentCommand& cmd) {
                return removeIBLComponent(cmd.entity);
            });

        dispatcher.registerQueryHandler<events::scene::HasIBLComponentQuery>(
            [this](const events::scene::HasIBLComponentQuery& query) {
                return hasIBLComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetIBLDataQuery>(
            [this](const events::scene::GetIBLDataQuery& query) {
                return getIBLData(query.entity);
            });
    }

}

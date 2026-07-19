#include "IBLComponentService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/project/SceneEvents.hpp"

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
        data.hdrRef = comp.hdrRef;
        data.intensity = comp.intensity;
        data.rotationDeg = comp.rotationDeg;
        data.tint = comp.tint;

        return data;
    }

    bool IBLComponentService::setIBLData(EntityHandle entity, const IBLData& ibl) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        components::IBLComponent& comp = sceneEntity.hasComponent<components::IBLComponent>()
            ? sceneEntity.getComponent<components::IBLComponent>()
            : sceneEntity.addComponent<components::IBLComponent>();
        comp.hdrRef = ibl.hdrRef;
        comp.intensity = ibl.intensity;
        comp.rotationDeg = ibl.rotationDeg;
        comp.tint = ibl.tint;

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

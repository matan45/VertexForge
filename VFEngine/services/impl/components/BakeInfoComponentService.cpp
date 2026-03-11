#include "BakeInfoComponentService.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/Entity.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/project/SceneEvents.hpp"

namespace services {

    void BakeInfoComponentService::registerEventHandlers(events::EventDispatcher& dispatcher) {
        dispatcher.registerQueryHandler<events::scene::HasNavmeshRootQuery>(
            [this](const events::scene::HasNavmeshRootQuery& query) {
                return hasNavmeshRoot(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetNavmeshRootDataQuery>(
            [this](const events::scene::GetNavmeshRootDataQuery& query) {
                return getNavmeshRootData(query.entity);
            });
    }

    bool BakeInfoComponentService::hasNavmeshRoot(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::NavmeshComponent>();
    }

    std::optional<NavmeshRootData> BakeInfoComponentService::getNavmeshRootData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::NavmeshComponent>()) {
            return std::nullopt;
        }

        auto& comp = sceneEntity.getComponent<components::NavmeshComponent>();
        NavmeshRootData data;
        data.navmeshPath = comp.navmeshPath;
        return data;
    }

}

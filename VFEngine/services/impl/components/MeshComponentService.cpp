#include "MeshComponentService.hpp"
#include "../../../utilities/scene/SceneGraphSystem.hpp"
#include "../../../utilities/scene/Entity.hpp"
#include "../../../utilities/scene/EntityRegistry.hpp"
#include "../../../utilities/components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/SceneEvents.hpp"

namespace services {

    MeshComponentService::MeshComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(std::move(sceneGraph)) {}

    std::optional<MeshData> MeshComponentService::getMeshData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::MeshComponent>()) {
            return std::nullopt;
        }

        auto& comp = sceneEntity.getComponent<components::MeshComponent>();
        MeshData data;
        data.meshPath = comp.meshPath;
        data.showBoundingBox = comp.showBoundingBox;

        return data;
    }

    bool MeshComponentService::setMeshData(EntityHandle entity, const MeshData& mesh) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::MeshComponent>()) {
            auto& comp = sceneEntity.getComponent<components::MeshComponent>();
            comp.meshPath = mesh.meshPath;
            comp.showBoundingBox = mesh.showBoundingBox;
        }
        else {
            auto& comp = sceneEntity.addComponent<components::MeshComponent>();
            comp.meshPath = mesh.meshPath;
            comp.showBoundingBox = mesh.showBoundingBox;
        }

        // Publish notification to allow preloading of mesh assets
        if (!mesh.meshPath.empty()) {
            events::scene::MeshDataChangedNotification notification;
            notification.entity = entity;
            notification.meshPath = mesh.meshPath;
            events::EventDispatcher::instance().publish(notification);
        }

        return true;
    }

    bool MeshComponentService::addMeshComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::MeshComponent>()) {
            sceneEntity.addComponent<components::MeshComponent>();
            return true;
        }

        return false;  // Already has mesh component
    }

    bool MeshComponentService::removeMeshComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::MeshComponent>()) {
            sceneEntity.removeComponent<components::MeshComponent>();

            events::scene::MeshDataChangedNotification notification;
            notification.entity = entity;
            notification.meshPath = "";
            events::EventDispatcher::instance().publish(notification);

            return true;
        }

        return false;
    }

    bool MeshComponentService::hasMeshComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::MeshComponent>();
    }

    void MeshComponentService::registerEventHandlers(events::EventDispatcher& dispatcher) {
        dispatcher.registerCommandHandler<events::scene::AddMeshComponentCommand>(
            [this](const events::scene::AddMeshComponentCommand& cmd) {
                return addMeshComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveMeshComponentCommand>(
            [this](const events::scene::RemoveMeshComponentCommand& cmd) {
                return removeMeshComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::SetMeshDataCommand>(
            [this](const events::scene::SetMeshDataCommand& cmd) {
                return setMeshData(cmd.entity, cmd.meshData);
            });

        dispatcher.registerQueryHandler<events::scene::HasMeshComponentQuery>(
            [this](const events::scene::HasMeshComponentQuery& query) {
                return hasMeshComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetMeshDataQuery>(
            [this](const events::scene::GetMeshDataQuery& query) {
                return getMeshData(query.entity);
            });
    }

}

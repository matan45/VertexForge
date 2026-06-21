#include "MeshComponentService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "resource/AssetLifecycleManager.hpp"

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
        data.meshRef = comp.meshRef;
        data.animatorRef = comp.animatorRef;
        data.retargetRef = comp.retargetRef;
        data.showBoundingBox = comp.showBoundingBox;
        data.applyRootMotion = comp.applyRootMotion;
        data.maxDrawDistance = comp.maxDrawDistance;
        data.submeshIndex = comp.submeshIndex;
        data.renderLayer = comp.renderLayer;

        return data;
    }

    bool MeshComponentService::setMeshData(EntityHandle entity, const MeshData& mesh) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        auto& lifecycle = resource::AssetLifecycleManager::instance();

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::MeshComponent>()) {
            auto& comp = sceneEntity.getComponent<components::MeshComponent>();

            // Release old assets
            if (comp.meshRef.isValid() && comp.meshRef != mesh.meshRef) {
                lifecycle.release(comp.meshRef.getGUID());
            }
            if (comp.animatorRef.isValid() && comp.animatorRef != mesh.animatorRef) {
                lifecycle.release(comp.animatorRef.getGUID());
            }
            if (comp.retargetRef.isValid() && comp.retargetRef != mesh.retargetRef) {
                lifecycle.release(comp.retargetRef.getGUID());
            }

            comp.meshRef = mesh.meshRef;
            comp.animatorRef = mesh.animatorRef;
            comp.retargetRef = mesh.retargetRef;
            comp.showBoundingBox = mesh.showBoundingBox;
            comp.applyRootMotion = mesh.applyRootMotion;
            comp.maxDrawDistance = mesh.maxDrawDistance;
            comp.submeshIndex = mesh.submeshIndex;
            comp.renderLayer = mesh.renderLayer;
        }
        else {
            auto& comp = sceneEntity.addComponent<components::MeshComponent>();
            comp.meshRef = mesh.meshRef;
            comp.animatorRef = mesh.animatorRef;
            comp.retargetRef = mesh.retargetRef;
            comp.showBoundingBox = mesh.showBoundingBox;
            comp.applyRootMotion = mesh.applyRootMotion;
            comp.maxDrawDistance = mesh.maxDrawDistance;
            comp.submeshIndex = mesh.submeshIndex;
            comp.renderLayer = mesh.renderLayer;
        }

        // Sync applyRootMotion to AnimatorComponent if it exists
        if (sceneEntity.hasComponent<components::AnimatorComponent>()) {
            auto& animComp = sceneEntity.getComponent<components::AnimatorComponent>();
            animComp.applyRootMotion = mesh.applyRootMotion;
        }

        // Acquire new assets
        if (mesh.meshRef.isValid()) {
            lifecycle.acquire(mesh.meshRef.getGUID(), resource::AssetType::Mesh);
        }
        if (mesh.animatorRef.isValid()) {
            lifecycle.acquire(mesh.animatorRef.getGUID(), resource::AssetType::Animator);
        }
        if (mesh.retargetRef.isValid()) {
            lifecycle.acquire(mesh.retargetRef.getGUID(), resource::AssetType::RetargetMap);
        }

        // Publish notification to allow preloading of mesh assets and animator cleanup
        events::scene::MeshDataChangedNotification notification;
        notification.entity = entity;
        notification.meshPath = mesh.meshRef.resolve();
        notification.animatorPath = mesh.animatorRef.resolve();
        events::EventDispatcher::instance().publish(notification);

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
            auto& comp = sceneEntity.getComponent<components::MeshComponent>();
            auto& lifecycle = resource::AssetLifecycleManager::instance();
            if (comp.meshRef.isValid()) {
                lifecycle.release(comp.meshRef.getGUID());
            }
            if (comp.animatorRef.isValid()) {
                lifecycle.release(comp.animatorRef.getGUID());
            }
            if (comp.retargetRef.isValid()) {
                lifecycle.release(comp.retargetRef.getGUID());
            }

            sceneEntity.removeComponent<components::MeshComponent>();

            events::scene::MeshDataChangedNotification notification;
            notification.entity = entity;
            notification.meshPath = "";
            notification.animatorPath = "";
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

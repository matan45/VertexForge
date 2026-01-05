#include "CameraComponentService.hpp"
#include "../../../utilities/scene/SceneGraphSystem.hpp"
#include "../../../utilities/scene/Entity.hpp"
#include "../../../utilities/scene/EntityRegistry.hpp"
#include "../../../utilities/components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/SceneEvents.hpp"

namespace services {

    CameraComponentService::CameraComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(std::move(sceneGraph)) {}

    std::optional<CameraData> CameraComponentService::getCameraData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::CameraComponent>()) {
            return std::nullopt;
        }

        auto& comp = sceneEntity.getComponent<components::CameraComponent>();
        CameraData data;
        data.fieldOfView = comp.fieldOfView;
        data.nearPlane = comp.nearPlane;
        data.farPlane = comp.farPlane;
        data.aspectRatio = comp.aspectRatio;
        data.isPerspective = comp.isPerspective;
        data.isPrimary = comp.isPrimary;
        data.showFrustum = comp.showFrustum;
        data.orthoSize = comp.orthoSize;

        return data;
    }

    bool CameraComponentService::setCameraData(EntityHandle entity, const CameraData& camera) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::CameraComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::CameraComponent>();
        comp.fieldOfView = camera.fieldOfView;
        comp.nearPlane = camera.nearPlane;
        comp.farPlane = camera.farPlane;
        comp.aspectRatio = camera.aspectRatio;
        comp.isPerspective = camera.isPerspective;
        comp.showFrustum = camera.showFrustum;
        comp.orthoSize = camera.orthoSize;

        // If setting this camera as primary, clear isPrimary from all other cameras
        if (camera.isPrimary && !comp.isPrimary) {
            auto view = registry.view<components::CameraComponent>();
            for (auto otherEntity : view) {
                if (otherEntity != internal::fromHandle(entity)) {
                    auto& otherComp = view.get<components::CameraComponent>(otherEntity);
                    otherComp.isPrimary = false;
                }
            }
        }
        comp.isPrimary = camera.isPrimary;
        comp.updateProjectionMatrix();

        return true;
    }

    std::optional<EntityHandle> CameraComponentService::getPrimaryCamera() const {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::CameraComponent>();

        // Find camera with isPrimary = true (skip inactive entities)
        for (auto entity : view) {
            if (registry.all_of<components::NameComponent>(entity)) {
                const auto& nameComp = registry.get<components::NameComponent>(entity);
                if (!nameComp.isActive) {
                    continue;
                }
            }

            const auto& comp = view.get<components::CameraComponent>(entity);
            if (comp.isPrimary) {
                return internal::toHandle(entity);
            }
        }

        // Fallback to first active camera if no primary is set
        for (auto entity : view) {
            if (registry.all_of<components::NameComponent>(entity)) {
                const auto& nameComp = registry.get<components::NameComponent>(entity);
                if (!nameComp.isActive) {
                    continue;
                }
            }
            return internal::toHandle(entity);
        }

        return std::nullopt;
    }

    bool CameraComponentService::addCameraComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        if (!sceneEntity.hasComponent<components::TransformComponent>()) {
            auto& transform = sceneEntity.addComponent<components::TransformComponent>();
            sceneEntity.addOrReplaceComponent<components::WorldTransformComponent>().worldMatrix = transform.getMatrix();
        }
        else if (!sceneEntity.hasComponent<components::WorldTransformComponent>()) {
            auto& transform = sceneEntity.getComponent<components::TransformComponent>();
            sceneEntity.addOrReplaceComponent<components::WorldTransformComponent>().worldMatrix = transform.getMatrix();
        }

        if (!sceneEntity.hasComponent<components::CameraComponent>()) {
            sceneEntity.addComponent<components::CameraComponent>();
            autoAttachBillboard(entity, static_cast<uint32_t>(components::BillboardIconType::Camera));
            return true;
        }

        return false;
    }

    bool CameraComponentService::removeCameraComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::CameraComponent>()) {
            sceneEntity.removeComponent<components::CameraComponent>();
            autoDetachBillboard(entity, static_cast<uint32_t>(components::BillboardIconType::Camera));
            return true;
        }

        return false;
    }

    bool CameraComponentService::hasCameraComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::CameraComponent>();
    }

    void CameraComponentService::autoAttachBillboard(EntityHandle entity, uint32_t iconType) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::BillboardComponent>()) {
            auto& billboard = sceneEntity.addComponent<components::BillboardComponent>();
            billboard.iconType = static_cast<components::BillboardIconType>(iconType);
            billboard.editorOnly = true;
            billboard.selectable = true;
        }
    }

    void CameraComponentService::autoDetachBillboard(EntityHandle entity, uint32_t iconType) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::BillboardComponent>()) {
            auto& billboard = sceneEntity.getComponent<components::BillboardComponent>();
            // Only remove if it matches the expected icon type (auto-attached billboard)
            if (billboard.iconType == static_cast<components::BillboardIconType>(iconType)) {
                sceneEntity.removeComponent<components::BillboardComponent>();
            }
        }
    }

    void CameraComponentService::registerEventHandlers(events::EventDispatcher& dispatcher) {
        dispatcher.registerCommandHandler<events::scene::AddCameraComponentCommand>(
            [this](const events::scene::AddCameraComponentCommand& cmd) {
                return addCameraComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveCameraComponentCommand>(
            [this](const events::scene::RemoveCameraComponentCommand& cmd) {
                return removeCameraComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::SetCameraDataCommand>(
            [this](const events::scene::SetCameraDataCommand& cmd) {
                return setCameraData(cmd.entity, cmd.cameraData);
            });

        dispatcher.registerQueryHandler<events::scene::GetCameraDataQuery>(
            [this](const events::scene::GetCameraDataQuery& query) {
                return getCameraData(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::HasCameraComponentQuery>(
            [this](const events::scene::HasCameraComponentQuery& query) {
                return hasCameraComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetPrimaryCameraQuery>(
            [this](const events::scene::GetPrimaryCameraQuery&) {
                return getPrimaryCamera();
            });
    }

}

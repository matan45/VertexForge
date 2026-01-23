#include "LightComponentService.hpp"
#include "../../../utilities/scene/SceneGraphSystem.hpp"
#include "../../../utilities/scene/Entity.hpp"
#include "../../../utilities/scene/EntityRegistry.hpp"
#include "../../../utilities/components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/SceneEvents.hpp"

namespace services {

    LightComponentService::LightComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(std::move(sceneGraph)) {}

    // ========== DIRECTIONAL LIGHT COMPONENT OPERATIONS ==========

    bool LightComponentService::addDirectionalLightComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::DirectionalLightComponent>()) {
            sceneEntity.addComponent<components::DirectionalLightComponent>();
            autoAttachBillboard(entity);
            return true;
        }
        return false;
    }

    bool LightComponentService::removeDirectionalLightComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::DirectionalLightComponent>()) {
            sceneEntity.removeComponent<components::DirectionalLightComponent>();
            // Only remove billboard if no other light component exists
            if (!hasAnyLightComponent(entity)) {
                autoDetachBillboard(entity);
            }
            return true;
        }
        return false;
    }

    bool LightComponentService::hasDirectionalLightComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::DirectionalLightComponent>();
    }

    std::optional<DirectionalLightData> LightComponentService::getDirectionalLightData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::DirectionalLightComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::DirectionalLightComponent>();
        DirectionalLightData data;
        data.color = comp.color;
        data.intensity = comp.intensity;
        return data;
    }

    bool LightComponentService::setDirectionalLightData(EntityHandle entity, const DirectionalLightData& lightData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::DirectionalLightComponent>()) {
            sceneEntity.addComponent<components::DirectionalLightComponent>();
        }

        auto& comp = sceneEntity.getComponent<components::DirectionalLightComponent>();
        comp.color = lightData.color;
        comp.intensity = lightData.intensity;
        return true;
    }

    // ========== POINT LIGHT COMPONENT OPERATIONS ==========

    bool LightComponentService::addPointLightComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::PointLightComponent>()) {
            sceneEntity.addComponent<components::PointLightComponent>();
            autoAttachBillboard(entity);
            return true;
        }
        return false;
    }

    bool LightComponentService::removePointLightComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::PointLightComponent>()) {
            sceneEntity.removeComponent<components::PointLightComponent>();
            // Only remove billboard if no other light component exists
            if (!hasAnyLightComponent(entity)) {
                autoDetachBillboard(entity);
            }
            return true;
        }
        return false;
    }

    bool LightComponentService::hasPointLightComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::PointLightComponent>();
    }

    std::optional<PointLightData> LightComponentService::getPointLightData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::PointLightComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::PointLightComponent>();
        PointLightData data;
        data.color = comp.color;
        data.intensity = comp.intensity;
        data.radius = comp.radius;
        return data;
    }

    bool LightComponentService::setPointLightData(EntityHandle entity, const PointLightData& lightData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::PointLightComponent>()) {
            sceneEntity.addComponent<components::PointLightComponent>();
        }

        auto& comp = sceneEntity.getComponent<components::PointLightComponent>();
        comp.color = lightData.color;
        comp.intensity = lightData.intensity;
        comp.radius = lightData.radius;
        return true;
    }

    // ========== SPOT LIGHT COMPONENT OPERATIONS ==========

    bool LightComponentService::addSpotLightComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::SpotLightComponent>()) {
            sceneEntity.addComponent<components::SpotLightComponent>();
            autoAttachBillboard(entity);
            return true;
        }
        return false;
    }

    bool LightComponentService::removeSpotLightComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::SpotLightComponent>()) {
            sceneEntity.removeComponent<components::SpotLightComponent>();
            // Only remove billboard if no other light component exists
            if (!hasAnyLightComponent(entity)) {
                autoDetachBillboard(entity);
            }
            return true;
        }
        return false;
    }

    bool LightComponentService::hasSpotLightComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::SpotLightComponent>();
    }

    std::optional<SpotLightData> LightComponentService::getSpotLightData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::SpotLightComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::SpotLightComponent>();
        SpotLightData data;
        data.color = comp.color;
        data.intensity = comp.intensity;
        data.innerAngle = comp.innerAngle;
        data.outerAngle = comp.outerAngle;
        data.range = comp.range;
        return data;
    }

    bool LightComponentService::setSpotLightData(EntityHandle entity, const SpotLightData& lightData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::SpotLightComponent>()) {
            sceneEntity.addComponent<components::SpotLightComponent>();
        }

        auto& comp = sceneEntity.getComponent<components::SpotLightComponent>();
        comp.color = lightData.color;
        comp.intensity = lightData.intensity;
        comp.innerAngle = lightData.innerAngle;
        comp.outerAngle = lightData.outerAngle;
        comp.range = lightData.range;
        return true;
    }

    // ========== HELPER METHODS ==========

    bool LightComponentService::hasAnyLightComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::DirectionalLightComponent>() ||
               sceneEntity.hasComponent<components::PointLightComponent>() ||
               sceneEntity.hasComponent<components::SpotLightComponent>();
    }

    void LightComponentService::autoAttachBillboard(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::BillboardComponent>()) {
            auto& billboard = sceneEntity.addComponent<components::BillboardComponent>();
            billboard.iconType = components::BillboardIconType::Light;
            billboard.editorOnly = true;
            billboard.selectable = true;
        }
    }

    void LightComponentService::autoDetachBillboard(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::BillboardComponent>()) {
            auto& billboard = sceneEntity.getComponent<components::BillboardComponent>();
            // Only remove if it's a light billboard (auto-attached)
            if (billboard.iconType == components::BillboardIconType::Light) {
                sceneEntity.removeComponent<components::BillboardComponent>();
            }
        }
    }

    void LightComponentService::registerEventHandlers(events::EventDispatcher& dispatcher) {
        // Directional Light component handlers
        dispatcher.registerCommandHandler<events::scene::AddDirectionalLightComponentCommand>(
            [this](const events::scene::AddDirectionalLightComponentCommand& cmd) {
                return addDirectionalLightComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveDirectionalLightComponentCommand>(
            [this](const events::scene::RemoveDirectionalLightComponentCommand& cmd) {
                return removeDirectionalLightComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::SetDirectionalLightDataCommand>(
            [this](const events::scene::SetDirectionalLightDataCommand& cmd) {
                return setDirectionalLightData(cmd.entity, cmd.lightData);
            });

        dispatcher.registerQueryHandler<events::scene::HasDirectionalLightComponentQuery>(
            [this](const events::scene::HasDirectionalLightComponentQuery& query) {
                return hasDirectionalLightComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetDirectionalLightDataQuery>(
            [this](const events::scene::GetDirectionalLightDataQuery& query) {
                return getDirectionalLightData(query.entity);
            });

        // Point Light component handlers
        dispatcher.registerCommandHandler<events::scene::AddPointLightComponentCommand>(
            [this](const events::scene::AddPointLightComponentCommand& cmd) {
                return addPointLightComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemovePointLightComponentCommand>(
            [this](const events::scene::RemovePointLightComponentCommand& cmd) {
                return removePointLightComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::SetPointLightDataCommand>(
            [this](const events::scene::SetPointLightDataCommand& cmd) {
                return setPointLightData(cmd.entity, cmd.lightData);
            });

        dispatcher.registerQueryHandler<events::scene::HasPointLightComponentQuery>(
            [this](const events::scene::HasPointLightComponentQuery& query) {
                return hasPointLightComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetPointLightDataQuery>(
            [this](const events::scene::GetPointLightDataQuery& query) {
                return getPointLightData(query.entity);
            });

        // Spot Light component handlers
        dispatcher.registerCommandHandler<events::scene::AddSpotLightComponentCommand>(
            [this](const events::scene::AddSpotLightComponentCommand& cmd) {
                return addSpotLightComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveSpotLightComponentCommand>(
            [this](const events::scene::RemoveSpotLightComponentCommand& cmd) {
                return removeSpotLightComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::SetSpotLightDataCommand>(
            [this](const events::scene::SetSpotLightDataCommand& cmd) {
                return setSpotLightData(cmd.entity, cmd.lightData);
            });

        dispatcher.registerQueryHandler<events::scene::HasSpotLightComponentQuery>(
            [this](const events::scene::HasSpotLightComponentQuery& query) {
                return hasSpotLightComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetSpotLightDataQuery>(
            [this](const events::scene::GetSpotLightDataQuery& query) {
                return getSpotLightData(query.entity);
            });
    }

}

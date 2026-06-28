#include "LightComponentService.hpp"
#include "BillboardAutoIcon.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../events/render/LightCullingEvents.hpp"
#include <algorithm>

namespace services {

    namespace {
        glm::vec3 validateColor(const glm::vec3& color) {
            return glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
        }

        bool isValidIntensity(float intensity) {
            return intensity >= 0.0f;
        }

        bool isValidRadius(float radius) {
            return radius > 0.0f;
        }

        bool isValidSpotAngles(float innerAngle, float outerAngle) {
            if (innerAngle < 0.0f || innerAngle > 90.0f) return false;
            if (outerAngle < 0.0f || outerAngle > 90.0f) return false;
            if (innerAngle >= outerAngle) return false;
            return true;
        }
    }

    // ========== Template Helpers ==========

    template<typename ComponentT>
    bool LightComponentService::addLightImpl(EntityHandle entity, uint8_t lightType, components::BillboardIconType iconType) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<ComponentT>()) {
            sceneEntity.addComponent<ComponentT>();
            components_helpers::autoAttachBillboard(entity, iconType);

            events::lighting::LightComponentChangedNotification notification;
            notification.entity = entity;
            notification.lightType = static_cast<events::lighting::LightType>(lightType);
            notification.added = true;
            events::EventDispatcher::instance().publish(notification);

            return true;
        }
        return false;
    }

    template<typename ComponentT>
    bool LightComponentService::removeLightImpl(EntityHandle entity, uint8_t lightType, components::BillboardIconType iconType) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<ComponentT>()) {
            sceneEntity.removeComponent<ComponentT>();
            components_helpers::autoDetachBillboard(
                entity,
                iconType,
                [this, entity](const scene::Entity&) { return hasAnyLightComponent(entity); });

            events::lighting::LightComponentChangedNotification notification;
            notification.entity = entity;
            notification.lightType = static_cast<events::lighting::LightType>(lightType);
            notification.added = false;
            events::EventDispatcher::instance().publish(notification);

            return true;
        }
        return false;
    }

    template<typename ComponentT>
    bool LightComponentService::hasLightImpl(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<ComponentT>();
    }

    // ========== Directional Light ==========

    bool LightComponentService::addDirectionalLightComponent(EntityHandle entity) {
        return addLightImpl<components::DirectionalLightComponent>(
            entity, static_cast<uint8_t>(events::lighting::LightType::Directional),
            components::BillboardIconType::DirectionalLight);
    }

    bool LightComponentService::removeDirectionalLightComponent(EntityHandle entity) {
        return removeLightImpl<components::DirectionalLightComponent>(
            entity, static_cast<uint8_t>(events::lighting::LightType::Directional),
            components::BillboardIconType::DirectionalLight);
    }

    bool LightComponentService::hasDirectionalLightComponent(EntityHandle entity) const {
        return hasLightImpl<components::DirectionalLightComponent>(entity);
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
        data.lightSize = comp.lightSize;
        data.showGizmo = comp.showGizmo;
        return data;
    }

    bool LightComponentService::setDirectionalLightData(EntityHandle entity, const DirectionalLightData& lightData) {
        if (!isValidIntensity(lightData.intensity)) {
            return false;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::DirectionalLightComponent>()) {
            sceneEntity.addComponent<components::DirectionalLightComponent>();
        }

        auto& comp = sceneEntity.getComponent<components::DirectionalLightComponent>();
        comp.color = validateColor(lightData.color);
        comp.intensity = lightData.intensity;
        comp.lightSize = lightData.lightSize;
        comp.showGizmo = lightData.showGizmo;

        events::lighting::LightDataChangedNotification notification;
        notification.entity = entity;
        notification.lightType = events::lighting::LightType::Directional;
        events::EventDispatcher::instance().publish(notification);

        return true;
    }

    // ========== Point Light ==========

    bool LightComponentService::addPointLightComponent(EntityHandle entity) {
        return addLightImpl<components::PointLightComponent>(
            entity, static_cast<uint8_t>(events::lighting::LightType::Point),
            components::BillboardIconType::PointLight);
    }

    bool LightComponentService::removePointLightComponent(EntityHandle entity) {
        return removeLightImpl<components::PointLightComponent>(
            entity, static_cast<uint8_t>(events::lighting::LightType::Point),
            components::BillboardIconType::PointLight);
    }

    bool LightComponentService::hasPointLightComponent(EntityHandle entity) const {
        return hasLightImpl<components::PointLightComponent>(entity);
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
        data.lightSize = comp.lightSize;
        data.castsShadow = comp.castsShadow;
        data.showGizmo = comp.showGizmo;
        return data;
    }

    bool LightComponentService::setPointLightData(EntityHandle entity, const PointLightData& lightData) {
        if (!isValidIntensity(lightData.intensity)) {
            return false;
        }
        if (!isValidRadius(lightData.radius)) {
            return false;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::PointLightComponent>()) {
            sceneEntity.addComponent<components::PointLightComponent>();
        }

        auto& comp = sceneEntity.getComponent<components::PointLightComponent>();
        comp.color = validateColor(lightData.color);
        comp.intensity = lightData.intensity;
        comp.radius = lightData.radius;
        comp.lightSize = lightData.lightSize;
        comp.castsShadow = lightData.castsShadow;
        comp.showGizmo = lightData.showGizmo;

        events::lighting::LightDataChangedNotification notification;
        notification.entity = entity;
        notification.lightType = events::lighting::LightType::Point;
        events::EventDispatcher::instance().publish(notification);

        return true;
    }

    // ========== Spot Light ==========

    bool LightComponentService::addSpotLightComponent(EntityHandle entity) {
        return addLightImpl<components::SpotLightComponent>(
            entity, static_cast<uint8_t>(events::lighting::LightType::Spot),
            components::BillboardIconType::SpotLight);
    }

    bool LightComponentService::removeSpotLightComponent(EntityHandle entity) {
        return removeLightImpl<components::SpotLightComponent>(
            entity, static_cast<uint8_t>(events::lighting::LightType::Spot),
            components::BillboardIconType::SpotLight);
    }

    bool LightComponentService::hasSpotLightComponent(EntityHandle entity) const {
        return hasLightImpl<components::SpotLightComponent>(entity);
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
        data.lightSize = comp.lightSize;
        data.castsShadow = comp.castsShadow;
        data.showGizmo = comp.showGizmo;
        return data;
    }

    bool LightComponentService::setSpotLightData(EntityHandle entity, const SpotLightData& lightData) {
        if (!isValidIntensity(lightData.intensity)) {
            return false;
        }
        if (!isValidRadius(lightData.range)) {
            return false;
        }
        if (!isValidSpotAngles(lightData.innerAngle, lightData.outerAngle)) {
            return false;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::SpotLightComponent>()) {
            sceneEntity.addComponent<components::SpotLightComponent>();
        }

        auto& comp = sceneEntity.getComponent<components::SpotLightComponent>();
        comp.color = validateColor(lightData.color);
        comp.intensity = lightData.intensity;
        comp.innerAngle = lightData.innerAngle;
        comp.outerAngle = lightData.outerAngle;
        comp.range = lightData.range;
        comp.lightSize = lightData.lightSize;
        comp.castsShadow = lightData.castsShadow;
        comp.showGizmo = lightData.showGizmo;

        events::lighting::LightDataChangedNotification notification;
        notification.entity = entity;
        notification.lightType = events::lighting::LightType::Spot;
        events::EventDispatcher::instance().publish(notification);

        return true;
    }

    // ========== Shadow Override ==========

    bool LightComponentService::hasShadowOverride(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::ShadowOverrideComponent>();
    }

    std::optional<ShadowOverrideData> LightComponentService::getShadowOverrideData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::ShadowOverrideComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::ShadowOverrideComponent>();
        ShadowOverrideData data;
        data.depthBias = comp.depthBias;
        data.slopeBias = comp.slopeBias;
        data.normalBias = comp.normalBias;
        data.maxPages = comp.maxPages;
        data.softShadows = comp.softShadows;
        data.hasSoftShadowOverride = comp.hasSoftShadowOverride;
        return data;
    }

    bool LightComponentService::setShadowOverrideData(EntityHandle entity, const ShadowOverrideData& overrideData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::ShadowOverrideComponent>()) {
            sceneEntity.addComponent<components::ShadowOverrideComponent>();
        }

        auto& comp = sceneEntity.getComponent<components::ShadowOverrideComponent>();
        comp.depthBias = overrideData.depthBias;
        comp.slopeBias = overrideData.slopeBias;
        comp.normalBias = overrideData.normalBias;
        comp.maxPages = overrideData.maxPages;
        comp.softShadows = overrideData.softShadows;
        comp.hasSoftShadowOverride = overrideData.hasSoftShadowOverride;
        return true;
    }

    bool LightComponentService::removeShadowOverride(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::ShadowOverrideComponent>()) {
            sceneEntity.removeComponent<components::ShadowOverrideComponent>();
            return true;
        }
        return false;
    }

    // ========== Shared Helpers ==========

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

    // ========== Event Handler Registration ==========

    void LightComponentService::registerEventHandlers(events::EventDispatcher& dispatcher) {
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

        dispatcher.registerQueryHandler<events::scene::HasShadowOverrideQuery>(
            [this](const events::scene::HasShadowOverrideQuery& query) {
                return hasShadowOverride(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetShadowOverrideDataQuery>(
            [this](const events::scene::GetShadowOverrideDataQuery& query) {
                return getShadowOverrideData(query.entity);
            });

        dispatcher.registerCommandHandler<events::scene::SetShadowOverrideDataCommand>(
            [this](const events::scene::SetShadowOverrideDataCommand& cmd) {
                return setShadowOverrideData(cmd.entity, cmd.data);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveShadowOverrideCommand>(
            [this](const events::scene::RemoveShadowOverrideCommand& cmd) {
                return removeShadowOverride(cmd.entity);
            });
    }

}

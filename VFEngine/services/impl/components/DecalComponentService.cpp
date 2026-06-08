#include "DecalComponentService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/scene/ComponentMediaEvents.hpp"
#include <algorithm>

namespace services {

    DecalComponentService::DecalComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(std::move(sceneGraph)) {}

    bool DecalComponentService::addDecalComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::DecalComponent>()) {
            sceneEntity.addComponent<components::DecalComponent>();
            return true;
        }
        return false;
    }

    bool DecalComponentService::removeDecalComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::DecalComponent>()) {
            sceneEntity.removeComponent<components::DecalComponent>();
            return true;
        }
        return false;
    }

    bool DecalComponentService::hasDecalComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::DecalComponent>();
    }

    std::optional<DecalData> DecalComponentService::getDecalData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::DecalComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::DecalComponent>();
        DecalData data;
        data.shape = static_cast<uint8_t>(comp.shape);
        data.halfExtents = comp.halfExtents;
        data.albedoTextureRef = comp.albedoTextureRef;
        data.normalTextureRef = comp.normalTextureRef;
        data.ormTextureRef = comp.ormTextureRef;
        data.color = comp.color;
        data.angleFadeStart = comp.angleFadeStart;
        data.angleFadeEnd = comp.angleFadeEnd;
        data.edgeFalloff = comp.edgeFalloff;
        data.sortPriority = comp.sortPriority;
        data.modifyNormals = comp.modifyNormals;
        data.normalStrength = comp.normalStrength;
        return data;
    }

    bool DecalComponentService::setDecalData(EntityHandle entity, const DecalData& decalData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::DecalComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::DecalComponent>();
        comp.shape = components::toDecalShape(decalData.shape);
        comp.halfExtents = decalData.halfExtents;
        comp.albedoTextureRef = decalData.albedoTextureRef;
        comp.normalTextureRef = decalData.normalTextureRef;
        comp.ormTextureRef = decalData.ormTextureRef;
        comp.color = decalData.color;
        comp.angleFadeStart = decalData.angleFadeStart;
        comp.angleFadeEnd = std::min(decalData.angleFadeEnd, decalData.angleFadeStart - 0.001f);
        comp.edgeFalloff = decalData.edgeFalloff;
        comp.sortPriority = decalData.sortPriority;
        comp.modifyNormals = decalData.modifyNormals;
        comp.normalStrength = decalData.normalStrength;
        return true;
    }

    void DecalComponentService::registerEventHandlers(events::EventDispatcher& dispatcher) {
        dispatcher.registerCommandHandler<events::scene::AddDecalComponentCommand>(
            [this](const events::scene::AddDecalComponentCommand& cmd) {
                return addDecalComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveDecalComponentCommand>(
            [this](const events::scene::RemoveDecalComponentCommand& cmd) {
                return removeDecalComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::SetDecalDataCommand>(
            [this](const events::scene::SetDecalDataCommand& cmd) {
                return setDecalData(cmd.entity, cmd.decalData);
            });

        dispatcher.registerQueryHandler<events::scene::HasDecalComponentQuery>(
            [this](const events::scene::HasDecalComponentQuery& query) {
                return hasDecalComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetDecalDataQuery>(
            [this](const events::scene::GetDecalDataQuery& query) {
                return getDecalData(query.entity);
            });
    }

}

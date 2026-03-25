#include "FogVolumeComponentService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/scene/FogVolumeEvents.hpp"

namespace services {

    FogVolumeComponentService::FogVolumeComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(std::move(sceneGraph)) {}

    bool FogVolumeComponentService::addFogVolumeComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::FogVolumeComponent>()) {
            sceneEntity.addComponent<components::FogVolumeComponent>();
            autoAttachBillboard(entity, static_cast<uint32_t>(components::BillboardIconType::FogVolume));
            return true;
        }
        return false;
    }

    bool FogVolumeComponentService::removeFogVolumeComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::FogVolumeComponent>()) {
            sceneEntity.removeComponent<components::FogVolumeComponent>();
            autoDetachBillboard(entity, static_cast<uint32_t>(components::BillboardIconType::FogVolume));
            return true;
        }
        return false;
    }

    bool FogVolumeComponentService::hasFogVolumeComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::FogVolumeComponent>();
    }

    std::optional<FogVolumeData> FogVolumeComponentService::getFogVolumeData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return std::nullopt;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::FogVolumeComponent>()) return std::nullopt;

        const auto& comp = sceneEntity.getComponent<components::FogVolumeComponent>();
        FogVolumeData data;
        data.shape = static_cast<uint8_t>(comp.shape);
        data.halfExtents = comp.halfExtents;
        data.density = comp.density;
        data.albedo = comp.albedo;
        data.emission = comp.emission;
        data.edgeFalloff = comp.edgeFalloff;
        data.blendMode = static_cast<uint8_t>(comp.blendMode);
        data.showGizmo = comp.showGizmo;
        return data;
    }

    bool FogVolumeComponentService::setFogVolumeData(EntityHandle entity, const FogVolumeData& data) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        // Note: auto-creates component if missing (deserialization path).
        // Billboard is NOT attached here — use addFogVolumeComponent() for editor creation.
        if (!sceneEntity.hasComponent<components::FogVolumeComponent>()) {
            sceneEntity.addComponent<components::FogVolumeComponent>();
        }

        auto& comp = sceneEntity.getComponent<components::FogVolumeComponent>();
        comp.shape = static_cast<components::FogVolumeShape>(data.shape);
        comp.halfExtents = data.halfExtents;
        comp.density = data.density;
        comp.albedo = data.albedo;
        comp.emission = data.emission;
        comp.edgeFalloff = data.edgeFalloff;
        comp.blendMode = static_cast<components::FogVolumeBlendMode>(data.blendMode);
        comp.showGizmo = data.showGizmo;
        return true;
    }

    void FogVolumeComponentService::autoAttachBillboard(EntityHandle entity, uint32_t iconType) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::BillboardComponent>()) {
            auto& billboard = sceneEntity.addComponent<components::BillboardComponent>();
            billboard.iconType = static_cast<components::BillboardIconType>(iconType);
            billboard.editorOnly = true;
            billboard.selectable = true;
        }
    }

    void FogVolumeComponentService::autoDetachBillboard(EntityHandle entity, uint32_t iconType) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::BillboardComponent>()) {
            auto& billboard = sceneEntity.getComponent<components::BillboardComponent>();
            if (billboard.iconType == static_cast<components::BillboardIconType>(iconType)) {
                sceneEntity.removeComponent<components::BillboardComponent>();
            }
        }
    }

    void FogVolumeComponentService::registerEventHandlers(events::EventDispatcher& dispatcher) {
        dispatcher.registerCommandHandler<events::scene::AddFogVolumeComponentCommand>(
            [this](const events::scene::AddFogVolumeComponentCommand& cmd) {
                return addFogVolumeComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveFogVolumeComponentCommand>(
            [this](const events::scene::RemoveFogVolumeComponentCommand& cmd) {
                return removeFogVolumeComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::SetFogVolumeDataCommand>(
            [this](const events::scene::SetFogVolumeDataCommand& cmd) {
                return setFogVolumeData(cmd.entity, cmd.data);
            });

        dispatcher.registerQueryHandler<events::scene::HasFogVolumeComponentQuery>(
            [this](const events::scene::HasFogVolumeComponentQuery& query) {
                return hasFogVolumeComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetFogVolumeDataQuery>(
            [this](const events::scene::GetFogVolumeDataQuery& query) {
                return getFogVolumeData(query.entity);
            });
    }

}

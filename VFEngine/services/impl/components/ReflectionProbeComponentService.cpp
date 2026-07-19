#include "ReflectionProbeComponentService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/scene/ReflectionProbeEvents.hpp"

namespace services {

    ReflectionProbeComponentService::ReflectionProbeComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(std::move(sceneGraph)) {}

    bool ReflectionProbeComponentService::addReflectionProbeComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::ReflectionProbeComponent>()) {
            sceneEntity.addComponent<components::ReflectionProbeComponent>();
            return true;
        }
        return false;
    }

    bool ReflectionProbeComponentService::removeReflectionProbeComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::ReflectionProbeComponent>()) {
            sceneEntity.removeComponent<components::ReflectionProbeComponent>();
            return true;
        }
        return false;
    }

    bool ReflectionProbeComponentService::hasReflectionProbeComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::ReflectionProbeComponent>();
    }

    std::optional<ReflectionProbeData> ReflectionProbeComponentService::getReflectionProbeData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return std::nullopt;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::ReflectionProbeComponent>()) return std::nullopt;

        const auto& comp = sceneEntity.getComponent<components::ReflectionProbeComponent>();
        ReflectionProbeData data;
        data.shape = static_cast<uint8_t>(comp.shape);
        data.halfExtents = comp.halfExtents;
        data.blendDistance = comp.blendDistance;
        data.intensity = comp.intensity;
        data.nearPlane = comp.nearPlane;
        data.farPlane = comp.farPlane;
        data.priority = comp.priority;
        data.captureShadows = comp.captureShadows;
        data.showGizmo = comp.showGizmo;
        return data;
    }

    bool ReflectionProbeComponentService::setReflectionProbeData(EntityHandle entity, const ReflectionProbeData& data) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        // Note: auto-creates component if missing (deserialization path).
        if (!sceneEntity.hasComponent<components::ReflectionProbeComponent>()) {
            sceneEntity.addComponent<components::ReflectionProbeComponent>();
        }

        auto& comp = sceneEntity.getComponent<components::ReflectionProbeComponent>();

        // Only the fields that change what the capture SEES force a re-bake. halfExtents,
        // blendDistance, intensity and priority are consumed per-frame from the probe SSBO, so
        // dragging those sliders must not kick off a 6-face scene re-render on every mouse move.
        const bool captureChanged = comp.nearPlane != data.nearPlane ||
                                    comp.farPlane != data.farPlane ||
                                    comp.captureShadows != data.captureShadows;

        comp.shape = static_cast<components::ReflectionProbeShape>(data.shape);
        comp.halfExtents = data.halfExtents;
        comp.blendDistance = data.blendDistance;
        comp.intensity = data.intensity;
        comp.nearPlane = data.nearPlane;
        comp.farPlane = data.farPlane;
        comp.priority = data.priority;
        comp.captureShadows = data.captureShadows;
        comp.showGizmo = data.showGizmo;

        if (captureChanged) {
            comp.dirty = true;
        }
        return true;
    }

    bool ReflectionProbeComponentService::requestBake(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();

        if (entity.isValid()) {
            if (!internal::isValidHandle(entity, registry)) return false;
            scene::Entity sceneEntity(internal::fromHandle(entity));
            if (!sceneEntity.hasComponent<components::ReflectionProbeComponent>()) return false;
            sceneEntity.getComponent<components::ReflectionProbeComponent>().dirty = true;
            return true;
        }

        // Bake All: flag every probe, including inactive ones — activating an entity later must not
        // silently leave it with a stale capture.
        bool any = false;
        auto view = registry.view<components::ReflectionProbeComponent>();
        for (auto e : view) {
            view.get<components::ReflectionProbeComponent>(e).dirty = true;
            any = true;
        }
        return any;
    }

    void ReflectionProbeComponentService::registerEventHandlers(events::EventDispatcher& dispatcher) {
        dispatcher.registerCommandHandler<events::scene::AddReflectionProbeComponentCommand>(
            [this](const events::scene::AddReflectionProbeComponentCommand& cmd) {
                return addReflectionProbeComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveReflectionProbeComponentCommand>(
            [this](const events::scene::RemoveReflectionProbeComponentCommand& cmd) {
                return removeReflectionProbeComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::SetReflectionProbeDataCommand>(
            [this](const events::scene::SetReflectionProbeDataCommand& cmd) {
                return setReflectionProbeData(cmd.entity, cmd.data);
            });

        dispatcher.registerCommandHandler<events::scene::BakeReflectionProbesCommand>(
            [this](const events::scene::BakeReflectionProbesCommand& cmd) {
                return requestBake(cmd.entity);
            });

        dispatcher.registerQueryHandler<events::scene::HasReflectionProbeComponentQuery>(
            [this](const events::scene::HasReflectionProbeComponentQuery& query) {
                return hasReflectionProbeComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetReflectionProbeDataQuery>(
            [this](const events::scene::GetReflectionProbeDataQuery& query) {
                return getReflectionProbeData(query.entity);
            });
    }

}

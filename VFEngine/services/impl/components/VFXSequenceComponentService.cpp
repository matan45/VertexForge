#include "VFXSequenceComponentService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/project/SceneEvents.hpp"

namespace services {

    VFXSequenceComponentService::VFXSequenceComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(std::move(sceneGraph)) {}

    bool VFXSequenceComponentService::addVFXSequenceComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::VFXSequenceComponent>()) {
            sceneEntity.addComponent<components::VFXSequenceComponent>();
            autoAttachBillboard(entity, static_cast<uint32_t>(components::BillboardIconType::Particle));
            return true;
        }
        return false;
    }

    bool VFXSequenceComponentService::removeVFXSequenceComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::VFXSequenceComponent>()) {
            sceneEntity.removeComponent<components::VFXSequenceComponent>();
            autoDetachBillboard(entity, static_cast<uint32_t>(components::BillboardIconType::Particle));
            return true;
        }
        return false;
    }

    bool VFXSequenceComponentService::hasVFXSequenceComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::VFXSequenceComponent>();
    }

    std::optional<VFXSequenceData> VFXSequenceComponentService::getVFXSequenceData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::VFXSequenceComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::VFXSequenceComponent>();
        VFXSequenceData data;
        data.sequenceRef = comp.sequenceRef;
        data.autoPlay = comp.autoPlay;
        data.loop = comp.loop;
        data.socketName = comp.socketName;
        data.triggers.reserve(comp.triggers.size());
        for (const auto& trigger : comp.triggers) {
            VFXSequenceTriggerData td;
            td.sequenceRef = trigger.sequenceRef;
            td.eventName = trigger.eventName;
            td.socketName = trigger.socketName;
            data.triggers.push_back(std::move(td));
        }
        return data;
    }

    bool VFXSequenceComponentService::setVFXSequenceData(EntityHandle entity, const VFXSequenceData& vfxSequenceData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::VFXSequenceComponent>()) {
            sceneEntity.addComponent<components::VFXSequenceComponent>();
        }

        auto& comp = sceneEntity.getComponent<components::VFXSequenceComponent>();
        comp.sequenceRef = vfxSequenceData.sequenceRef;
        comp.autoPlay = vfxSequenceData.autoPlay;
        comp.loop = vfxSequenceData.loop;
        comp.socketName = vfxSequenceData.socketName;
        comp.triggers.clear();
        comp.triggers.reserve(vfxSequenceData.triggers.size());
        for (const auto& td : vfxSequenceData.triggers) {
            components::VFXSequenceTrigger trigger;
            trigger.eventName = td.eventName;
            trigger.sequenceRef = td.sequenceRef;
            trigger.socketName = td.socketName;
            comp.triggers.push_back(std::move(trigger));
        }
        // runtimeComboId is transient — never written from the DTO path.
        return true;
    }

    void VFXSequenceComponentService::autoAttachBillboard(EntityHandle entity, uint32_t iconType) {
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

    void VFXSequenceComponentService::autoDetachBillboard(EntityHandle entity, uint32_t iconType) {
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

    void VFXSequenceComponentService::registerEventHandlers(events::EventDispatcher& dispatcher) {
        dispatcher.registerCommandHandler<events::scene::AddVFXSequenceComponentCommand>(
            [this](const events::scene::AddVFXSequenceComponentCommand& cmd) {
                return addVFXSequenceComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveVFXSequenceComponentCommand>(
            [this](const events::scene::RemoveVFXSequenceComponentCommand& cmd) {
                return removeVFXSequenceComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::SetVFXSequenceDataCommand>(
            [this](const events::scene::SetVFXSequenceDataCommand& cmd) {
                return setVFXSequenceData(cmd.entity, cmd.vfxSequenceData);
            });

        dispatcher.registerQueryHandler<events::scene::HasVFXSequenceComponentQuery>(
            [this](const events::scene::HasVFXSequenceComponentQuery& query) {
                return hasVFXSequenceComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetVFXSequenceDataQuery>(
            [this](const events::scene::GetVFXSequenceDataQuery& query) {
                return getVFXSequenceData(query.entity);
            });
    }

}

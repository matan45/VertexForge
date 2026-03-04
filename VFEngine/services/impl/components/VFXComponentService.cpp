#include "VFXComponentService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/project/SceneEvents.hpp"

namespace services {

    VFXComponentService::VFXComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(std::move(sceneGraph)) {}

    bool VFXComponentService::addVFXComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::VFXComponent>()) {
            sceneEntity.addComponent<components::VFXComponent>();
            autoAttachBillboard(entity, static_cast<uint32_t>(components::BillboardIconType::Particle));
            return true;
        }
        return false;
    }

    bool VFXComponentService::removeVFXComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::VFXComponent>()) {
            sceneEntity.removeComponent<components::VFXComponent>();
            autoDetachBillboard(entity, static_cast<uint32_t>(components::BillboardIconType::Particle));
            return true;
        }
        return false;
    }

    bool VFXComponentService::hasVFXComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::VFXComponent>();
    }

    std::optional<VFXData> VFXComponentService::getVFXData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::VFXComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::VFXComponent>();
        VFXData data;
        data.vfxPath = comp.vfxPath;
        data.autoPlay = comp.autoPlay;
        data.loop = comp.loop;
        return data;
    }

    bool VFXComponentService::setVFXData(EntityHandle entity, const VFXData& vfxData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::VFXComponent>()) {
            sceneEntity.addComponent<components::VFXComponent>();
        }

        auto& comp = sceneEntity.getComponent<components::VFXComponent>();
        comp.vfxPath = vfxData.vfxPath;
        comp.autoPlay = vfxData.autoPlay;
        comp.loop = vfxData.loop;
        return true;
    }

    void VFXComponentService::autoAttachBillboard(EntityHandle entity, uint32_t iconType) {
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

    void VFXComponentService::autoDetachBillboard(EntityHandle entity, uint32_t iconType) {
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

    void VFXComponentService::registerEventHandlers(events::EventDispatcher& dispatcher) {
        dispatcher.registerCommandHandler<events::scene::AddVFXComponentCommand>(
            [this](const events::scene::AddVFXComponentCommand& cmd) {
                return addVFXComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveVFXComponentCommand>(
            [this](const events::scene::RemoveVFXComponentCommand& cmd) {
                return removeVFXComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::SetVFXDataCommand>(
            [this](const events::scene::SetVFXDataCommand& cmd) {
                return setVFXData(cmd.entity, cmd.vfxData);
            });

        dispatcher.registerQueryHandler<events::scene::HasVFXComponentQuery>(
            [this](const events::scene::HasVFXComponentQuery& query) {
                return hasVFXComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetVFXDataQuery>(
            [this](const events::scene::GetVFXDataQuery& query) {
                return getVFXData(query.entity);
            });
    }

}

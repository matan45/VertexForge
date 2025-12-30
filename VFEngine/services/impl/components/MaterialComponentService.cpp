#include "MaterialComponentService.hpp"
#include "../../../utilities/scene/SceneGraphSystem.hpp"
#include "../../../utilities/scene/Entity.hpp"
#include "../../../utilities/scene/EntityRegistry.hpp"
#include "../../../utilities/components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/MaterialEvents.hpp"

namespace services {

    MaterialComponentService::MaterialComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(std::move(sceneGraph)) {}

    bool MaterialComponentService::addMaterialComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::MaterialComponent>()) {
            sceneEntity.addComponent<components::MaterialComponent>();
            return true;
        }
        return false;
    }

    bool MaterialComponentService::removeMaterialComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::MaterialComponent>()) {
            sceneEntity.removeComponent<components::MaterialComponent>();
            return true;
        }
        return false;
    }

    bool MaterialComponentService::hasMaterialComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::MaterialComponent>();
    }

    std::optional<MaterialData> MaterialComponentService::getMaterialData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::MaterialComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::MaterialComponent>();
        MaterialData data;
        data.defaultMaterial = comp.defaultMaterial;
        data.subMeshMaterials = comp.subMeshMaterials;
        data.parameterOverrides = comp.parameterOverrides;
        return data;
    }

    bool MaterialComponentService::setMaterialData(EntityHandle entity, const MaterialData& material) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::MaterialComponent>()) {
            sceneEntity.addComponent<components::MaterialComponent>();
        }

        auto& comp = sceneEntity.getComponent<components::MaterialComponent>();
        comp.defaultMaterial = material.defaultMaterial;
        comp.subMeshMaterials = material.subMeshMaterials;
        comp.parameterOverrides = material.parameterOverrides;
        return true;
    }

    bool MaterialComponentService::setDefaultMaterial(EntityHandle entity, const std::string& materialPath) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::MaterialComponent>()) {
            sceneEntity.addComponent<components::MaterialComponent>();
        }

        auto& comp = sceneEntity.getComponent<components::MaterialComponent>();
        comp.setDefaultMaterial(materialPath);
        return true;
    }

    bool MaterialComponentService::setSubMeshMaterial(EntityHandle entity, const std::string& submeshName, const std::string& materialPath) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::MaterialComponent>()) {
            sceneEntity.addComponent<components::MaterialComponent>();
        }

        auto& comp = sceneEntity.getComponent<components::MaterialComponent>();
        if (materialPath.empty()) {
            // Clear the assignment
            comp.subMeshMaterials.erase(submeshName);
        } else {
            comp.setSubMeshMaterial(submeshName, materialPath);
        }
        return true;
    }

    std::string MaterialComponentService::getSubMeshMaterial(EntityHandle entity, const std::string& submeshName) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return "";
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::MaterialComponent>()) {
            return "";
        }

        const auto& comp = sceneEntity.getComponent<components::MaterialComponent>();
        return comp.getMaterialForSubmesh(submeshName);
    }

    std::map<std::string, std::string> MaterialComponentService::getAllSubMeshMaterials(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return {};
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::MaterialComponent>()) {
            return {};
        }

        const auto& comp = sceneEntity.getComponent<components::MaterialComponent>();
        return comp.subMeshMaterials;
    }

    void MaterialComponentService::registerEventHandlers(events::EventDispatcher& dispatcher) {
        dispatcher.registerCommandHandler<events::material::AddMaterialComponentCommand>(
            [this](const events::material::AddMaterialComponentCommand& cmd) {
                return addMaterialComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::material::RemoveMaterialComponentCommand>(
            [this](const events::material::RemoveMaterialComponentCommand& cmd) {
                return removeMaterialComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::material::SetMaterialDataCommand>(
            [this](const events::material::SetMaterialDataCommand& cmd) {
                return setMaterialData(cmd.entity, cmd.materialData);
            });

        dispatcher.registerCommandHandler<events::material::SetDefaultMaterialCommand>(
            [this](const events::material::SetDefaultMaterialCommand& cmd) {
                return setDefaultMaterial(cmd.entity, cmd.materialPath);
            });

        dispatcher.registerCommandHandler<events::material::SetSubMeshMaterialCommand>(
            [this](const events::material::SetSubMeshMaterialCommand& cmd) {
                return setSubMeshMaterial(cmd.entity, cmd.submeshName, cmd.materialPath);
            });

        dispatcher.registerQueryHandler<events::material::HasMaterialComponentQuery>(
            [this](const events::material::HasMaterialComponentQuery& query) {
                return hasMaterialComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::material::GetMaterialDataQuery>(
            [this](const events::material::GetMaterialDataQuery& query) {
                return getMaterialData(query.entity);
            });

        dispatcher.registerQueryHandler<events::material::GetSubMeshMaterialQuery>(
            [this](const events::material::GetSubMeshMaterialQuery& query) {
                return getSubMeshMaterial(query.entity, query.submeshName);
            });

        dispatcher.registerQueryHandler<events::material::GetAllSubMeshMaterialsQuery>(
            [this](const events::material::GetAllSubMeshMaterialsQuery& query) {
                return getAllSubMeshMaterials(query.entity);
            });
    }

}

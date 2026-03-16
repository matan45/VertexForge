#include "MaterialComponentService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/render/MaterialEvents.hpp"
#include "resource/AssetLifecycleManager.hpp"
#include <asset/AssetRef.hpp>

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
            auto& comp = sceneEntity.getComponent<components::MaterialComponent>();
            auto& lifecycle = resource::AssetLifecycleManager::instance();
            if (comp.defaultMaterialRef.isValid()) lifecycle.release(comp.defaultMaterialRef.getGUID());
            for (const auto& [name, ref] : comp.subMeshMaterials) {
                if (ref.isValid()) lifecycle.release(ref.getGUID());
            }
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
        data.defaultMaterialRef = comp.defaultMaterialRef;
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
        auto& lifecycle = resource::AssetLifecycleManager::instance();
        // Release old materials that are changing
        if (comp.defaultMaterialRef.isValid() && comp.defaultMaterialRef != material.defaultMaterialRef) {
            lifecycle.release(comp.defaultMaterialRef.getGUID());
        }
        for (const auto& [name, ref] : comp.subMeshMaterials) {
            if (ref.isValid()) {
                auto it = material.subMeshMaterials.find(name);
                if (it == material.subMeshMaterials.end() || it->second != ref) {
                    lifecycle.release(ref.getGUID());
                }
            }
        }
        // Acquire new materials
        if (material.defaultMaterialRef.isValid() && material.defaultMaterialRef != comp.defaultMaterialRef) {
            lifecycle.acquire(material.defaultMaterialRef.getGUID(), resource::AssetType::Material);
        }
        for (const auto& [name, ref] : material.subMeshMaterials) {
            if (ref.isValid()) {
                auto it = comp.subMeshMaterials.find(name);
                if (it == comp.subMeshMaterials.end() || it->second != ref) {
                    lifecycle.acquire(ref.getGUID(), resource::AssetType::Material);
                }
            }
        }
        comp.defaultMaterialRef = material.defaultMaterialRef;
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
        auto& lifecycle = resource::AssetLifecycleManager::instance();
        auto newRef = asset::AssetRef::fromPath(materialPath);
        if (comp.defaultMaterialRef.isValid() && comp.defaultMaterialRef != newRef) {
            lifecycle.release(comp.defaultMaterialRef.getGUID());
        }
        if (newRef.isValid() && newRef != comp.defaultMaterialRef) {
            lifecycle.acquire(newRef.getGUID(), resource::AssetType::Material);
        }
        comp.setDefaultMaterial(newRef);
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
        auto& lifecycle = resource::AssetLifecycleManager::instance();
        auto newRef = asset::AssetRef::fromPath(materialPath);
        auto it = comp.subMeshMaterials.find(submeshName);
        if (it != comp.subMeshMaterials.end() && it->second.isValid() && it->second != newRef) {
            lifecycle.release(it->second.getGUID());
        }
        if (materialPath.empty()) {
            comp.subMeshMaterials.erase(submeshName);
        } else {
            bool isNew = (it == comp.subMeshMaterials.end() || it->second != newRef);
            comp.setSubMeshMaterial(submeshName, newRef);
            if (isNew) {
                lifecycle.acquire(newRef.getGUID(), resource::AssetType::Material);
            }
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
        return comp.getMaterialForSubmesh(submeshName).resolve();
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
        std::map<std::string, std::string> result;
        for (const auto& [name, ref] : comp.subMeshMaterials) {
            result[name] = ref.resolve();
        }
        return result;
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

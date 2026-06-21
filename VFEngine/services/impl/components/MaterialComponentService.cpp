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
        // VK-1418: expose per-slot RTT bindings (name + runtime handle) to the editor.
        for (const auto& [slotKey, binding] : comp.renderTextureSlotBindings) {
            RenderTextureSlotBindingData out;
            out.sourceName = binding.sourceName;
            out.source = (binding.source != entt::null)
                ? EntityHandle{static_cast<uint64_t>(binding.source)}
                : EntityHandle::invalid();
            data.renderTextureSlotBindings[slotKey] = std::move(out);
        }
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

        // VK-1418: apply per-slot RTT bindings. Store the source name and resolve the runtime
        // entity handle from it (mirrors BillboardComponentService); empty names drop the binding.
        comp.renderTextureSlotBindings.clear();
        for (const auto& [slotKey, in] : material.renderTextureSlotBindings) {
            if (in.sourceName.empty()) {
                continue;
            }
            components::MaterialComponent::RenderTextureSlotBinding binding;
            binding.sourceName = in.sourceName;
            binding.source = entt::null;
            auto nameView = registry.view<components::NameComponent, components::RenderTextureComponent>();
            for (auto e : nameView) {
                if (nameView.get<components::NameComponent>(e).name == binding.sourceName) {
                    binding.source = e;
                    break;
                }
            }
            comp.renderTextureSlotBindings[slotKey] = std::move(binding);
        }
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

    bool MaterialComponentService::setMaterialParameter(EntityHandle entity, const std::string& parameterName,
                                                        const ::material::ParameterValue& value) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry) || parameterName.empty()) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::MaterialComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::MaterialComponent>();
        comp.parameterOverrides[parameterName] = value;

        events::material::MaterialParameterChangedNotification notification;
        notification.entity = entity;
        notification.parameterName = parameterName;
        notification.value = value;
        events::EventDispatcher::instance().publish(notification);
        return true;
    }

    bool MaterialComponentService::clearMaterialParameter(EntityHandle entity, const std::string& parameterName) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::MaterialComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::MaterialComponent>();
        if (parameterName.empty()) {
            if (comp.parameterOverrides.empty()) return false;
            comp.parameterOverrides.clear();
        } else if (comp.parameterOverrides.erase(parameterName) == 0) {
            return false;
        }

        events::material::MaterialParameterChangedNotification notification;
        notification.entity = entity;
        notification.parameterName = parameterName;
        notification.cleared = true;
        events::EventDispatcher::instance().publish(notification);
        return true;
    }

    std::optional<::material::ParameterValue> MaterialComponentService::getMaterialParameter(
        EntityHandle entity, const std::string& parameterName) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::MaterialComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::MaterialComponent>();
        auto it = comp.parameterOverrides.find(parameterName);
        if (it == comp.parameterOverrides.end()) {
            return std::nullopt;
        }
        return it->second;
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

        dispatcher.registerCommandHandler<events::material::SetMaterialParameterCommand>(
            [this](const events::material::SetMaterialParameterCommand& cmd) {
                return setMaterialParameter(cmd.entity, cmd.parameterName, cmd.value);
            });

        dispatcher.registerCommandHandler<events::material::ClearMaterialParameterCommand>(
            [this](const events::material::ClearMaterialParameterCommand& cmd) {
                return clearMaterialParameter(cmd.entity, cmd.parameterName);
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

        dispatcher.registerQueryHandler<events::material::GetMaterialParameterQuery>(
            [this](const events::material::GetMaterialParameterQuery& query) {
                return getMaterialParameter(query.entity, query.parameterName);
            });
    }

}

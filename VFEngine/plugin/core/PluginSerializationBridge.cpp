#include "PluginSerializationBridge.hpp"
#include "PluginComponentRegistry.hpp"
#include "serialization/SceneSerialization.hpp"
#include "components/PluginComponents.hpp"
#include "components/PluginPropertyTypes.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "events/EventDispatcher.hpp"
#include "events/plugin/PluginComponentEvents.hpp"
#include "print/Log.hpp"

namespace plugin
{
    void PluginSerializationBridge::init()
    {
        serialization::SceneSerialization::setPluginSerializationHooks(
            // Serialize hook
            [](scene::Entity& entity) -> nlohmann::json
            {
                nlohmann::json result = nlohmann::json::object();
                if (!entity.hasComponent<components::PluginComponentsComponent>())
                    return result;

                auto& pluginComp = entity.getComponent<components::PluginComponentsComponent>();

                for (const auto& [qualifiedName, data] : pluginComp.components)
                {
                    result["plugin:" + qualifiedName] = data;
                }
                return result;
            },
            // Deserialize hook
            [](const nlohmann::json& pluginEntries, scene::Entity& entity)
            {
                auto& registry = PluginComponentRegistry::instance();
                auto& pluginComp = entity.addOrReplaceComponent<components::PluginComponentsComponent>();

                for (const auto& [key, value] : pluginEntries.items())
                {
                    std::string qualifiedName = key.substr(7); // strip "plugin:"
                    const auto* info = registry.findComponent(qualifiedName);

                    if (info)
                    {
                        // Merge with defaults (handles new properties added after save)
                        nlohmann::json merged = info->defaultData;
                        for (const auto& [k, v] : value.items())
                        {
                            merged[k] = v;
                        }
                        pluginComp.components[qualifiedName] = std::move(merged);
                    }
                    else
                    {
                        pluginComp.components[qualifiedName] = value;
                    }
                }
            }
        );

        // Register CQRS event handlers
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerQueryHandler<events::plugin::GetRegisteredPluginComponentsQuery>(
            [](const events::plugin::GetRegisteredPluginComponentsQuery&)
            {
                return PluginComponentRegistry::instance().getAllComponentNames();
            });

        dispatcher.registerQueryHandler<events::plugin::GetPluginComponentDataQuery>(
            [](const events::plugin::GetPluginComponentDataQuery& query) -> std::optional<nlohmann::json>
            {
                auto& reg = scene::EntityRegistry::getRegistry();
                auto entity = static_cast<entt::entity>(query.entity.id);

                if (!reg.valid(entity) || !reg.all_of<components::PluginComponentsComponent>(entity))
                    return std::nullopt;

                auto& pluginComp = reg.get<components::PluginComponentsComponent>(entity);
                auto it = pluginComp.components.find(query.qualifiedName);
                if (it != pluginComp.components.end())
                    return it->second;

                return std::nullopt;
            });

        dispatcher.registerCommandHandler<events::plugin::AddPluginComponentCommand>(
            [](const events::plugin::AddPluginComponentCommand& cmd) -> bool
            {
                auto& reg = scene::EntityRegistry::getRegistry();
                auto entity = static_cast<entt::entity>(cmd.entity.id);

                if (!reg.valid(entity))
                    return false;

                const auto* info = PluginComponentRegistry::instance().findComponent(cmd.qualifiedName);
                if (!info)
                    return false;

                auto& pluginComp = reg.get_or_emplace<components::PluginComponentsComponent>(entity);
                if (pluginComp.components.contains(cmd.qualifiedName))
                    return false;

                pluginComp.components[cmd.qualifiedName] = info->defaultData;
                return true;
            });

        dispatcher.registerCommandHandler<events::plugin::RemovePluginComponentCommand>(
            [](const events::plugin::RemovePluginComponentCommand& cmd) -> bool
            {
                auto& reg = scene::EntityRegistry::getRegistry();
                auto entity = static_cast<entt::entity>(cmd.entity.id);

                if (!reg.valid(entity) || !reg.all_of<components::PluginComponentsComponent>(entity))
                    return false;

                auto& pluginComp = reg.get<components::PluginComponentsComponent>(entity);
                bool erased = pluginComp.components.erase(cmd.qualifiedName) > 0;

                if (pluginComp.components.empty())
                    reg.remove<components::PluginComponentsComponent>(entity);

                return erased;
            });

        dispatcher.registerCommandHandler<events::plugin::UpdatePluginComponentDataCommand>(
            [](const events::plugin::UpdatePluginComponentDataCommand& cmd)
            {
                auto& reg = scene::EntityRegistry::getRegistry();
                auto entity = static_cast<entt::entity>(cmd.entity.id);

                if (!reg.valid(entity) || !reg.all_of<components::PluginComponentsComponent>(entity))
                    return;

                auto& pluginComp = reg.get<components::PluginComponentsComponent>(entity);
                auto it = pluginComp.components.find(cmd.qualifiedName);
                if (it != pluginComp.components.end())
                    it->second = cmd.data;
            });

        dispatcher.registerQueryHandler<events::plugin::GetPluginComponentDescriptorsQuery>(
            [](const events::plugin::GetPluginComponentDescriptorsQuery& query)
                -> std::vector<components::plugin::PropertyDescriptor>
            {
                const auto* info = PluginComponentRegistry::instance().findComponent(query.qualifiedName);
                if (info)
                    return info->properties;
                return {};
            });

        dispatcher.registerQueryHandler<events::plugin::HasPluginInspectorQuery>(
            [](const events::plugin::HasPluginInspectorQuery& query) -> bool
            {
                const auto* info = PluginComponentRegistry::instance().findComponent(query.qualifiedName);
                return info && info->inspector != nullptr;
            });

        dispatcher.registerCommandHandler<events::plugin::InvokePluginInspectorCommand>(
            [](const events::plugin::InvokePluginInspectorCommand& cmd) -> bool
            {
                auto& reg = scene::EntityRegistry::getRegistry();
                auto entity = static_cast<entt::entity>(cmd.entity.id);

                if (!reg.valid(entity) || !reg.all_of<components::PluginComponentsComponent>(entity))
                    return false;

                const auto* info = PluginComponentRegistry::instance().findComponent(cmd.qualifiedName);
                if (!info || !info->inspector)
                    return false;

                auto& pluginComp = reg.get<components::PluginComponentsComponent>(entity);
                auto it = pluginComp.components.find(cmd.qualifiedName);
                if (it == pluginComp.components.end())
                    return false;

                return info->inspector(it->second);
            });
    }

    void PluginSerializationBridge::shutdown()
    {
        serialization::SceneSerialization::setPluginSerializationHooks(nullptr, nullptr);
    }
}

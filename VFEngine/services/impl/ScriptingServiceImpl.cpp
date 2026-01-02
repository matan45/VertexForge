#include "ScriptingServiceImpl.hpp"
#include "../events/ScriptingEvents.hpp"
#include "../events/EventDispatcher.hpp"
#include "../data/EntityConversion.hpp"
#include "../../utilities/scene/EntityRegistry.hpp"
#include "../../utilities/components/Components.hpp"
#include <cassert>
#include <spdlog/spdlog.h>

namespace services {

    using internal::fromHandle;
    using internal::toHandle;

    ScriptingServiceImpl::ScriptingServiceImpl(IScriptingProvider* scriptingProvider,
                                                std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : scriptingProvider(scriptingProvider)
        , sceneGraph(sceneGraph) {
        assert(scriptingProvider && "ScriptingProvider must not be null");
        assert(sceneGraph && "SceneGraphSystem must not be null");
    }

    ScriptingServiceImpl::~ScriptingServiceImpl() = default;

    void ScriptingServiceImpl::registerEventHandlers() {
        auto& dispatcher = ::events::EventDispatcher::instance();

        // === Commands ===
        dispatcher.registerCommandHandler<events::scripting::AttachScriptCommand>(
            [this](const auto& cmd) {
                return attachScript(cmd.entity, cmd.data);
            });

        dispatcher.registerCommandHandler<events::scripting::DetachScriptCommand>(
            [this](const auto& cmd) {
                detachScript(cmd.entity, cmd.scriptPath);
                return true;
            });

        dispatcher.registerCommandHandler<events::scripting::SetScriptEnabledCommand>(
            [this](const auto& cmd) {
                setScriptEnabled(cmd.entity, cmd.scriptPath, cmd.enabled);
            });

        dispatcher.registerCommandHandler<events::scripting::SetScriptPropertyCommand>(
            [this](const auto& cmd) {
                return setProperty(cmd.entity, cmd.scriptPath, cmd.propertyName, cmd.value);
            });

        dispatcher.registerCommandHandler<events::scripting::CallScriptMethodCommand>(
            [this](const auto& cmd) {
                return callMethod(cmd.entity, cmd.scriptPath, cmd.methodName, cmd.args);
            });

        dispatcher.registerCommandHandler<events::scripting::TriggerScriptStartCommand>(
            [this](const auto& cmd) {
                triggerStart(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scripting::TriggerScriptDestroyCommand>(
            [this](const auto& cmd) {
                triggerDestroy(cmd.entity);
            });

        // === Queries ===
        dispatcher.registerQueryHandler<events::scripting::HasScriptQuery>(
            [this](const auto& query) {
                return hasScript(query.entity, query.scriptPath);
            });

        dispatcher.registerQueryHandler<events::scripting::IsScriptEnabledQuery>(
            [this](const auto& query) {
                return isScriptEnabled(query.entity, query.scriptPath);
            });

        dispatcher.registerQueryHandler<events::scripting::GetScriptPathsQuery>(
            [this](const auto& query) {
                return getScriptPaths(query.entity);
            });

        dispatcher.registerQueryHandler<events::scripting::GetScriptPropertiesQuery>(
            [this](const auto& query) {
                return getScriptProperties(query.entity, query.scriptPath);
            });

        dispatcher.registerQueryHandler<events::scripting::GetScriptPropertyQuery>(
            [this](const auto& query) {
                return getProperty(query.entity, query.scriptPath, query.propertyName);
            });

        dispatcher.registerQueryHandler<events::scripting::GetScriptMethodsQuery>(
            [this](const auto& query) {
                return getScriptMethods(query.entity, query.scriptPath);
            });

        dispatcher.registerQueryHandler<events::scripting::GetScriptDataQuery>(
            [this](const auto& query) -> std::optional<ScriptComponentData> {
                auto enttEntity = internal::fromHandle(query.entity);
                auto& registry = scene::EntityRegistry::getRegistry();
                if (!registry.all_of<components::ScriptComponent>(enttEntity)) {
                    return std::nullopt;
                }
                // Return first script's data, or empty data if no scripts
                const auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);
                ScriptComponentData data;
                if (!scriptComp.scripts.empty()) {
                    data.scriptPath = scriptComp.scripts[0].scriptPath;
                    data.enabled = scriptComp.scripts[0].enabled;
                }
                return data;
            });
    }

    bool ScriptingServiceImpl::attachScript(EntityHandle entity, const ScriptData& data) {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        // Add ScriptComponent if not present
        if (!registry.all_of<components::ScriptComponent>(enttEntity)) {
            registry.emplace<components::ScriptComponent>(enttEntity);
        }

        // If no script path provided, just ensure component exists (for "Add Component" UI flow)
        if (data.scriptPath.empty()) {
            spdlog::info("[ScriptingService] Added empty ScriptComponent to entity");
            return true;
        }

        auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);

        // Check if script already attached
        if (scriptComp.hasScript(data.scriptPath)) {
            spdlog::warn("[ScriptingService] Script '{}' already attached to entity", data.scriptPath);
            return false;
        }

        // Load the script via provider
        auto info = scriptingProvider->loadScript(data.scriptPath, entity);
        if (!info.has_value()) {
            // Script failed to load - publish error notification
            auto error = scriptingProvider->getLastError();
            if (error.has_value()) {
                events::scripting::ScriptErrorNotification errorNotification;
                errorNotification.entity = entity;
                errorNotification.error = error.value();
                ::events::EventDispatcher::instance().publish(errorNotification);
            }
            return false;
        }

        // Create new script entry
        components::ScriptEntry entry;
        entry.scriptPath = data.scriptPath;
        entry.enabled = data.enabled;
        entry.started = false;
        entry.instanceId = info->instanceId;
        entry.hasOnStart = info->hasOnStart;
        entry.hasOnUpdate = info->hasOnUpdate;
        entry.hasOnDestroy = info->hasOnDestroy;

        scriptComp.scripts.push_back(entry);

        // Publish success notification
        events::scripting::ScriptAttachedNotification attachedNotification;
        attachedNotification.entity = entity;
        attachedNotification.scriptPath = data.scriptPath;
        attachedNotification.className = info->className;
        ::events::EventDispatcher::instance().publish(attachedNotification);

        spdlog::info("[ScriptingService] Attached script '{}' to entity (total scripts: {})",
                     data.scriptPath, scriptComp.scripts.size());
        return true;
    }

    void ScriptingServiceImpl::detachScript(EntityHandle entity, const std::string& scriptPath) {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.all_of<components::ScriptComponent>(enttEntity)) {
            return;
        }

        auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);
        auto* entry = scriptComp.findByPath(scriptPath);

        if (!entry) {
            return;
        }

        // Call onDestroy if script was started
        if (entry->started && entry->hasOnDestroy) {
            scriptingProvider->callOnDestroy(entry->instanceId);
        }

        // Unload from provider
        scriptingProvider->unloadScript(entry->instanceId);

        // Remove from component
        scriptComp.removeByPath(scriptPath);

        // Remove component entirely if no scripts left
        if (scriptComp.scripts.empty()) {
            registry.remove<components::ScriptComponent>(enttEntity);
        }

        // Publish notification
        events::scripting::ScriptDetachedNotification detachedNotification;
        detachedNotification.entity = entity;
        detachedNotification.scriptPath = scriptPath;
        ::events::EventDispatcher::instance().publish(detachedNotification);

        spdlog::info("[ScriptingService] Detached script '{}' from entity", scriptPath);
    }

    void ScriptingServiceImpl::detachAllScripts(EntityHandle entity) {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.all_of<components::ScriptComponent>(enttEntity)) {
            return;
        }

        auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);

        // Detach each script
        for (auto& entry : scriptComp.scripts) {
            if (entry.started && entry.hasOnDestroy) {
                scriptingProvider->callOnDestroy(entry.instanceId);
            }
            scriptingProvider->unloadScript(entry.instanceId);

            events::scripting::ScriptDetachedNotification detachedNotification;
            detachedNotification.entity = entity;
            detachedNotification.scriptPath = entry.scriptPath;
            ::events::EventDispatcher::instance().publish(detachedNotification);
        }

        // Remove component
        registry.remove<components::ScriptComponent>(enttEntity);

        spdlog::info("[ScriptingService] Detached all scripts from entity");
    }

    bool ScriptingServiceImpl::hasScripts(EntityHandle entity) const {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.all_of<components::ScriptComponent>(enttEntity)) {
            return false;
        }

        return !registry.get<components::ScriptComponent>(enttEntity).scripts.empty();
    }

    bool ScriptingServiceImpl::hasScript(EntityHandle entity, const std::string& scriptPath) const {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.all_of<components::ScriptComponent>(enttEntity)) {
            return false;
        }

        return registry.get<components::ScriptComponent>(enttEntity).hasScript(scriptPath);
    }

    std::vector<std::string> ScriptingServiceImpl::getScriptPaths(EntityHandle entity) const {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();
        std::vector<std::string> paths;

        if (!registry.all_of<components::ScriptComponent>(enttEntity)) {
            return paths;
        }

        const auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);
        for (const auto& entry : scriptComp.scripts) {
            paths.push_back(entry.scriptPath);
        }
        return paths;
    }

    void ScriptingServiceImpl::setScriptEnabled(EntityHandle entity, const std::string& scriptPath, bool enabled) {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.all_of<components::ScriptComponent>(enttEntity)) {
            return;
        }

        auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);
        auto* entry = scriptComp.findByPath(scriptPath);
        if (entry) {
            entry->enabled = enabled;
        }
    }

    bool ScriptingServiceImpl::isScriptEnabled(EntityHandle entity, const std::string& scriptPath) const {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.all_of<components::ScriptComponent>(enttEntity)) {
            return false;
        }

        const auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);
        const auto* entry = scriptComp.findByPath(scriptPath);
        return entry ? entry->enabled : false;
    }

    std::vector<ScriptPropertyInfo> ScriptingServiceImpl::getScriptProperties(EntityHandle entity,
                                                                               const std::string& scriptPath) const {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.all_of<components::ScriptComponent>(enttEntity)) {
            return {};
        }

        const auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);
        const auto* entry = scriptComp.findByPath(scriptPath);
        if (entry) {
            return scriptingProvider->getProperties(entry->instanceId);
        }
        return {};
    }

    bool ScriptingServiceImpl::setProperty(EntityHandle entity, const std::string& scriptPath,
                                            const std::string& propertyName, const std::any& value) {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.all_of<components::ScriptComponent>(enttEntity)) {
            return false;
        }

        const auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);
        const auto* entry = scriptComp.findByPath(scriptPath);
        if (entry) {
            return scriptingProvider->setProperty(entry->instanceId, propertyName, value);
        }
        return false;
    }

    std::optional<std::any> ScriptingServiceImpl::getProperty(EntityHandle entity, const std::string& scriptPath,
                                                               const std::string& propertyName) const {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.all_of<components::ScriptComponent>(enttEntity)) {
            return std::nullopt;
        }

        const auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);
        const auto* entry = scriptComp.findByPath(scriptPath);
        if (entry) {
            return scriptingProvider->getProperty(entry->instanceId, propertyName);
        }
        return std::nullopt;
    }

    std::vector<ScriptMethodInfo> ScriptingServiceImpl::getScriptMethods(EntityHandle entity,
                                                                          const std::string& scriptPath) const {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.all_of<components::ScriptComponent>(enttEntity)) {
            return {};
        }

        const auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);
        const auto* entry = scriptComp.findByPath(scriptPath);
        if (entry) {
            return scriptingProvider->getMethods(entry->instanceId);
        }
        return {};
    }

    std::optional<std::any> ScriptingServiceImpl::callMethod(EntityHandle entity, const std::string& scriptPath,
                                                              const std::string& methodName,
                                                              const std::vector<std::any>& args) {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.all_of<components::ScriptComponent>(enttEntity)) {
            return std::nullopt;
        }

        const auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);
        const auto* entry = scriptComp.findByPath(scriptPath);
        if (entry) {
            return scriptingProvider->callMethod(entry->instanceId, methodName, args);
        }
        return std::nullopt;
    }

    void ScriptingServiceImpl::sendMessage(EntityHandle entity, const std::string& messageName,
                                            const std::any& data) {
        // TODO: Implement message sending to all scripts on entity
    }

    void ScriptingServiceImpl::broadcastMessage(const std::string& messageName,
                                                 const std::any& data) {
        // TODO: Implement broadcast message to all scripts
    }

    void ScriptingServiceImpl::updateScripts(float deltaTime) {
        auto& registry = scene::EntityRegistry::getRegistry();

        // Iterate all entities with ScriptComponent
        auto view = registry.view<components::ScriptComponent>();

        for (auto entity : view) {
            auto& scriptComp = view.get<components::ScriptComponent>(entity);

            // Update each script on this entity
            for (auto& entry : scriptComp.scripts) {
                if (!entry.enabled) {
                    continue;
                }

                // Call onStart if not started yet
                if (!entry.started && entry.hasOnStart) {
                    spdlog::info("[ScriptingService] Calling onStart for script '{}' (instance {})",
                                 entry.scriptPath, entry.instanceId);
                    scriptingProvider->callOnStart(entry.instanceId);
                    entry.started = true;

                    // Publish notification
                    events::scripting::ScriptStartedNotification startedNotification;
                    startedNotification.entity = toHandle(entity);
                    startedNotification.scriptPath = entry.scriptPath;
                    ::events::EventDispatcher::instance().publish(startedNotification);
                }

                // Call onUpdate
                if (entry.hasOnUpdate) {
                    scriptingProvider->callOnUpdate(entry.instanceId, deltaTime);
                }
            }
        }
    }

    void ScriptingServiceImpl::fixedUpdate(float fixedDeltaTime) {
        // TODO: Implement fixedUpdate when needed
    }

    void ScriptingServiceImpl::lateUpdate(float deltaTime) {
        // TODO: Implement lateUpdate when needed
    }

    void ScriptingServiceImpl::triggerStart(EntityHandle entity) {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.all_of<components::ScriptComponent>(enttEntity)) {
            return;
        }

        auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);
        for (auto& entry : scriptComp.scripts) {
            if (!entry.started && entry.hasOnStart) {
                scriptingProvider->callOnStart(entry.instanceId);
                entry.started = true;
            }
        }
    }

    void ScriptingServiceImpl::triggerDestroy(EntityHandle entity) {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.all_of<components::ScriptComponent>(enttEntity)) {
            return;
        }

        auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);
        for (auto& entry : scriptComp.scripts) {
            if (entry.hasOnDestroy) {
                scriptingProvider->callOnDestroy(entry.instanceId);
            }
        }
    }

    bool ScriptingServiceImpl::reloadScript(const std::string& scriptPath) {
        // TODO: Implement hot-reload
        return false;
    }

    void ScriptingServiceImpl::reloadAllScripts() {
        // TODO: Implement hot-reload all
    }

}
